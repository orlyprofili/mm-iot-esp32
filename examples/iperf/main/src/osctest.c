/*
 * HaLow iperf + OSC demo (performance-max version)
 * SPDX-License-Identifier: Apache-2.0
 */
#include <endian.h>
#include <string.h>
#include <sys/time.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "mmosal.h"
#include "mmwlan.h"
#include "mmipal.h"
#include "mmiperf.h"
#include "mm_app_common.h"
#include "esp_timer.h"

/* --------------------------------------------------------------------------
 * CONFIG
 * -------------------------------------------------------------------------- */
#define SERVER_IP "10.0.0.15"
#define OSC_DEST_IP "255.255.255.255"
#define OSC_DEST_PORT 8000
#define OSC_TASK_STACK_W 4096
#define PING_TASK_STACK_W 4096
#define OSC_TASK_PRIORITY MMOSAL_TASK_PRI_LOW
#define PING_TASK_PRIORITY MMOSAL_TASK_PRI_NORM

#define ECHO_HOST_IP SERVER_IP
#define ECHO_PORT 9000
#define PING_INTERVAL_MS 50  /* send a ping every 50 ms */
#define PING_TIMEOUT_MS 1000 /* give up after 1 s       */

/* --------------------------------------------------------------------------
 * IPERF (unchanged)
 * -------------------------------------------------------------------------- */
enum iperf_type
{
    IPERF_TCP_SERVER,
    IPERF_UDP_SERVER,
    IPERF_TCP_CLIENT,
    IPERF_UDP_CLIENT
};

#ifndef IPERF_TYPE
#define IPERF_TYPE IPERF_UDP_SERVER
#endif
#ifndef IPERF_SERVER_IP
#define IPERF_SERVER_IP SERVER_IP
#endif
#ifndef IPERF_TIME_AMOUNT
#define IPERF_TIME_AMOUNT -10
#endif
#ifndef IPERF_SERVER_PORT
#define IPERF_SERVER_PORT 5001
#endif

static const char units[] = {' ', 'K', 'M', 'G', 'T'};
static uint32_t fmt_bytes(uint64_t b, uint8_t *u)
{
    *u = 0;
    while (b >= 1000 && *u < 4)
    {
        b /= 1000;
        (*u)++;
    }
    return b;
}

static void iperf_report(const struct mmiperf_report *r, void *, mmiperf_handle_t)
{
    uint8_t u;
    uint32_t v = fmt_bytes(r->bytes_transferred, &u);
    printf("\nIperf %s→%s  %lu %cB  %lu ms  %lu kbps\n",
           r->remote_addr, r->local_addr, v, units[u],
           r->duration_ms, r->bandwidth_kbitpsec);
    if (r->report_type == MMIPERF_UDP_DONE_SERVER ||
        r->report_type == MMIPERF_TCP_DONE_SERVER)
        printf("Waiting…\n");
}
static void start_tcp_client(void)
{
    struct mmiperf_client_args a = MMIPERF_CLIENT_ARGS_DEFAULT;
    strncpy(a.server_addr, IPERF_SERVER_IP, sizeof(a.server_addr));
    a.server_port = IPERF_SERVER_PORT;
    a.amount = (IPERF_TIME_AMOUNT < 0) ? IPERF_TIME_AMOUNT * 100 : IPERF_TIME_AMOUNT;
    a.report_fn = iperf_report;
    mmiperf_start_tcp_client(&a);
    printf("TCP client…");
}
static void start_udp_client(void)
{
    struct mmiperf_client_args a = MMIPERF_CLIENT_ARGS_DEFAULT;
    strncpy(a.server_addr, IPERF_SERVER_IP, sizeof(a.server_addr));
    a.server_port = IPERF_SERVER_PORT;
    a.amount = (IPERF_TIME_AMOUNT < 0) ? IPERF_TIME_AMOUNT * 100 : IPERF_TIME_AMOUNT;
    a.report_fn = iperf_report;
    mmiperf_start_udp_client(&a);
    printf("UDP client…");
}
static void start_tcp_server(void)
{
    struct mmiperf_server_args a = MMIPERF_SERVER_ARGS_DEFAULT;
    a.local_port = IPERF_SERVER_PORT;
    a.report_fn = iperf_report;
    if (!mmiperf_start_tcp_server(&a))
    {
        printf("TCP srv fail");
        return;
    }
    printf("TCP server waiting…");
}
static void start_udp_server(void)
{
    struct mmiperf_server_args a = MMIPERF_SERVER_ARGS_DEFAULT;
    a.local_port = IPERF_SERVER_PORT;
    a.report_fn = iperf_report;
    if (!mmiperf_start_udp_server(&a))
    {
        printf("UDP srv fail");
        return;
    }
    printf("UDP server waiting…");
}

/* --------------------------------------------------------------------------
 * OSC TASK (int-only, 100 Hz triangle)
 * -------------------------------------------------------------------------- */
static inline uint32_t be_float(float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    return htonl(v);
}
/* Pre-built table: 0.00→1.00→0.00 in 100 steps (1 Hz) */
/* Values are IEEE 754 float in big-endian format */
static const uint32_t osc_lut_be[100] = {
    0x00000000, 0x3ca3d70a, 0x3d23d70a, 0x3d8f5c29, 0x3dd2f1aa, 0x3e0a3d71, 0x3e2e147b, 0x3e51eb85, 0x3e75c28f, 0x3e851eb8,
    0x3e8a3d71, 0x3e8f5c29, 0x3e947ae1, 0x3e99999a, 0x3e9eb852, 0x3ea3d70a, 0x3ea8f5c3, 0x3eae147b, 0x3eb33333, 0x3eb851ec,
    0x3ebd70a4, 0x3ec28f5c, 0x3ec7ae14, 0x3ecccccd, 0x3ed1eb85, 0x3ed70a3d, 0x3edc28f6, 0x3ee147ae, 0x3ee66666, 0x3eeb851f,
    0x3ef0a3d7, 0x3ef5c28f, 0x3efae148, 0x3f000000, 0x3f028f5c, 0x3f051eb8, 0x3f07ae14, 0x3f0a3d71, 0x3f0ccccd, 0x3f0f5c29,
    0x3f11eb85, 0x3f147ae1, 0x3f170a3d, 0x3f19999a, 0x3f1c28f6, 0x3f1eb852, 0x3f2147ae, 0x3f23d70a, 0x3f266666, 0x3f28f5c3,
    0x3f2b851f, 0x3f2e147b, 0x3f30a3d7, 0x3f333333, 0x3f35c28f, 0x3f3851ec, 0x3f3ae148, 0x3f3d70a4, 0x3f400000, 0x3f428f5c,
    0x3f451eb8, 0x3f47ae14, 0x3f4a3d71, 0x3f4ccccd, 0x3f4f5c29, 0x3f51eb85, 0x3f547ae1, 0x3f570a3d, 0x3f59999a, 0x3f5c28f6,
    0x3f5eb852, 0x3f6147ae, 0x3f63d70a, 0x3f666666, 0x3f68f5c3, 0x3f6b851f, 0x3f6e147b, 0x3f70a3d7, 0x3f733333, 0x3f75c28f,
    0x3f7851ec, 0x3f7ae148, 0x3f7d70a4, 0x3f800000, 0x3f7d70a4, 0x3f7ae148, 0x3f7851ec, 0x3f75c28f, 0x3f733333, 0x3f70a3d7,
    0x3f6e147b, 0x3f6b851f, 0x3f68f5c3, 0x3f666666, 0x3f63d70a, 0x3f6147ae, 0x3f5eb852, 0x3f5c28f6, 0x3f59999a, 0x3f570a3d
};

static void osc_task(void *)
{
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0)
    {
        vTaskDelete(NULL);
    }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    struct sockaddr_in dst = {.sin_family = AF_INET, .sin_port = htons(OSC_DEST_PORT)};
    inet_aton(OSC_DEST_IP, &dst.sin_addr);
    char pkt[16];
    memset(pkt, 0, sizeof(pkt));
    memcpy(pkt, "/lfo", 4);   /* OSC address      */
    memcpy(pkt + 8, ",f", 2); /* type-tag “,f”    */
    size_t idx = 0;
    for (;;)
    {
        uint32_t be = htonl(osc_lut_be[idx]); /* ensure big-endian on the wire */
        memcpy(pkt + 12, &be, 4);
        sendto(s, pkt, sizeof(pkt), 0, (struct sockaddr *)&dst, sizeof(dst));
        idx = (idx + 1) % 100;
        vTaskDelay(pdMS_TO_TICKS(10)); /* 100 Hz => 1 Hz full cycle */
    }
}

/* --------------------------------------------------------------------------
 * UDP PING TASK (integer math, timeout)
 * -------------------------------------------------------------------------- */

static void udp_ping_task(void *)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in dst = {.sin_family = AF_INET, .sin_port = htons(ECHO_PORT)};
    inet_aton(ECHO_HOST_IP, &dst.sin_addr);
    struct timeval tv = {.tv_sec = PING_TIMEOUT_MS / 1000, .tv_usec = (PING_TIMEOUT_MS % 1000) * 1000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint32_t seq = 0;
    char buf[32];
    for (;;)
    {
        seq++;
        uint32_t t0 = (uint32_t)esp_timer_get_time(); /* µs */
        memcpy(buf, &seq, 4);
        memcpy(buf + 4, &t0, 4);
        sendto(sock, buf, 8, 0, (struct sockaddr *)&dst, sizeof(dst));

        struct sockaddr_in src;
        socklen_t sl = sizeof(src);
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &sl);
        if (n == 8 && memcmp(&seq, buf, 4) == 0)
        {
            uint32_t t1 = esp_timer_get_time(), t_sent;
            memcpy(&t_sent, buf + 4, 4);
            uint32_t diff_us = t1 - t_sent;
            printf("RTT %u: %u.%02u ms\n", (unsigned int)seq, (unsigned int)(diff_us / 1000), (unsigned int)((diff_us % 1000) / 10));
        }
        else
        {
            printf("RTT %u: timeout / lost\n", (unsigned int)seq);
        }
        vTaskDelay(pdMS_TO_TICKS(PING_INTERVAL_MS));
    }
}

/* --------------------------------------------------------------------------
 * MAIN
 * -------------------------------------------------------------------------- */
void app_main(void)
{
    printf("\nHaLow OSC + iperf demo (perf-max build " __DATE__ " " __TIME__ ")\n\n");
    app_wlan_init();
    app_wlan_start();

    switch (IPERF_TYPE)
    {
    case IPERF_TCP_SERVER:
        start_tcp_server();
        break;
    case IPERF_UDP_SERVER:
        start_udp_server();
        break;
    case IPERF_TCP_CLIENT:
        start_tcp_client();
        break;
    case IPERF_UDP_CLIENT:
        start_udp_client();
        break;
    }
    
    #ifdef UDP_PING_TASK
    /* Run this on machine
    # very tiny Python 3 UDP echo
    python - <<'PY'
    import socket,sys
    s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    s.bind(('',9000))
    while True:
        data,addr=s.recvfrom(1024)
        s.sendto(data,addr)
    PY

    */
    xTaskCreatePinnedToCore(udp_ping_task, "ping", PING_TASK_STACK_W, NULL, PING_TASK_PRIORITY, NULL, 1);
    #endif
    xTaskCreatePinnedToCore(osc_task, "osc", OSC_TASK_STACK_W, NULL, OSC_TASK_PRIORITY, NULL, 1);
}
