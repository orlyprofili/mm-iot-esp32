/*
 * Morse Micro HaLow demo – iperf plus a 1 Hz OSC beacon.
 *
 * Build / flash on the ESP32-S3 + MM6108 board, then on macOS:
 *
 *     brew install liblo           # one-time
 *     oscdump udp://255.255.255.255:8000
 *
 * You should see one line per second:
 *     /lfo f 0.00
 *     /lfo f 0.50
 *     /lfo f 1.00
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <endian.h>
#include <string.h>
#include <math.h>
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
static void udp_ping_task(void *arg);
static void osc_task(void *arg);
/* --------------------------------------------------------------------------
 * OSC SETTINGS
 * -------------------------------------------------------------------------- */
#define OSC_DEST_IP "255.255.255.255" /* broadcast – any host can listen  */
#define OSC_DEST_PORT 8000
#define OSC_TASK_STACK_W 1024 /* stack in 32-bit words            */
#define OSC_TASK_PRIORITY MMOSAL_TASK_PRI_LOW


/* --------------------------------------------------------------------------
 * ORIGINAL IPERF CONFIGURATION (unchanged)
 * -------------------------------------------------------------------------- */
enum iperf_type
{
    IPERF_TCP_SERVER,
    IPERF_UDP_SERVER,
    IPERF_TCP_CLIENT,
    IPERF_UDP_CLIENT,
};

#ifndef IPERF_TYPE
#define IPERF_TYPE IPERF_UDP_SERVER
#endif

#ifndef IPERF_SERVER_IP
#define IPERF_SERVER_IP "10.0.0.15"
#endif

#ifndef IPERF_TIME_AMOUNT
#define IPERF_TIME_AMOUNT -10
#endif

#ifndef IPERF_SERVER_PORT
#define IPERF_SERVER_PORT 5001
#endif

static const char units[] = {' ', 'K', 'M', 'G', 'T'};

/* ---------- convenience helper ---------- */
static uint32_t format_bytes(uint64_t bytes, uint8_t *unit_idx)
{
    MMOSAL_ASSERT(unit_idx);
    *unit_idx = 0;
    while (bytes >= 1000 && *unit_idx < 4)
    {
        bytes /= 1000;
        (*unit_idx)++;
    }
    return bytes;
}

/* ---------- iperf callbacks & launchers ---------- */
static void iperf_report_handler(const struct mmiperf_report *r,
                                 void *arg, mmiperf_handle_t h)
{
    (void)arg;
    (void)h;
    uint8_t u = 0;
    uint32_t b = format_bytes(r->bytes_transferred, &u);

    printf("\nIperf Report\n"
           "  Remote : %s:%d\n"
           "  Local  : %s:%d\n"
           "  Xfer   : %lu %cB, dur %lu ms, bw %lu kbps\n\n",
           r->remote_addr, r->remote_port,
           r->local_addr, r->local_port,
           b, units[u], r->duration_ms, r->bandwidth_kbitpsec);

    if (r->report_type == MMIPERF_UDP_DONE_SERVER ||
        r->report_type == MMIPERF_TCP_DONE_SERVER)
        printf("Waiting for next client…\n");
}

static void start_tcp_client(void)
{
    struct mmiperf_client_args a = MMIPERF_CLIENT_ARGS_DEFAULT;
    strncpy(a.server_addr, IPERF_SERVER_IP, sizeof(a.server_addr));
    a.server_port = IPERF_SERVER_PORT;
    a.amount = (IPERF_TIME_AMOUNT < 0) ? IPERF_TIME_AMOUNT * 100
                                       : IPERF_TIME_AMOUNT;
    a.report_fn = iperf_report_handler;
    mmiperf_start_tcp_client(&a);
    printf("\nIperf TCP client started…\n");
}

static void start_udp_client(void)
{
    struct mmiperf_client_args a = MMIPERF_CLIENT_ARGS_DEFAULT;
    strncpy(a.server_addr, IPERF_SERVER_IP, sizeof(a.server_addr));
    a.server_port = IPERF_SERVER_PORT;
    a.amount = (IPERF_TIME_AMOUNT < 0) ? IPERF_TIME_AMOUNT * 100
                                       : IPERF_TIME_AMOUNT;
    a.report_fn = iperf_report_handler;
    mmiperf_start_udp_client(&a);
    printf("\nIperf UDP client started…\n");
}

static void start_tcp_server(void)
{
    struct mmiperf_server_args a = MMIPERF_SERVER_ARGS_DEFAULT;
    a.local_port = IPERF_SERVER_PORT;
    a.report_fn = iperf_report_handler;
    if (!mmiperf_start_tcp_server(&a))
    {
        printf("Failed to start TCP server\n");
        return;
    }
    printf("\nIperf TCP server waiting…\n");
}

static void start_udp_server(void)
{
    struct mmiperf_server_args a = MMIPERF_SERVER_ARGS_DEFAULT;
    a.local_port = IPERF_SERVER_PORT;
    a.report_fn = iperf_report_handler;
    if (!mmiperf_start_udp_server(&a))
    {
        printf("Failed to start UDP server\n");
        return;
    }
    printf("\nIperf UDP server waiting…\n");
}

/* --------------------------------------------------------------------------
 * MAIN ENTRY
 * -------------------------------------------------------------------------- */
void app_main(void)
{
    printf("\nHaLow OSC + iperf demo (built " __DATE__ " " __TIME__ ")\n\n");

    app_wlan_init();
    app_wlan_start(); /* blocks until connected */

    switch ((enum iperf_type)IPERF_TYPE)
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

#define PING_TEST
    /* launch UDP ping task */
#ifdef PING_TEST
    mmosal_task_create(udp_ping_task,
                       NULL,
                       OSC_TASK_PRIORITY,
                       OSC_TASK_STACK_W,
                       "udp_ping_task");
#endif

#define OSC_TEST

#ifdef OSC_TEST
    /* launch OSC beacon task */
    mmosal_task_create(osc_task,
                       NULL,
                       OSC_TASK_PRIORITY,
                       OSC_TASK_STACK_W,
                       "osc_task");
#endif
}

/* --------------------------------------------------------------------------
 * OSC TASK
 * -------------------------------------------------------------------------- */
static void build_osc_packet(char *buf, float v)
{
    /* address “/lfo”, padded to 8 bytes */
    memcpy(buf, "/lfo\0\0\0", 8);

    /* type tag “,f”, padded to 4 bytes */
    memcpy(buf + 8, ",f\0\0", 4);

    /* big-endian float payload */
    uint32_t be;
    memcpy(&be, &v, 4);
    be = htonl(be);
    memcpy(buf + 12, &be, 4);
}

static void osc_task(void *arg)
{
    (void)arg;
    int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s < 0)
    {
        printf("OSC: socket() failed\n");
        vTaskDelete(NULL);
    }

    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    struct sockaddr_in dst = {
        .sin_family = AF_INET,
        .sin_port = htons(OSC_DEST_PORT)};
    inet_aton(OSC_DEST_IP, &dst.sin_addr);

    char pkt[16];
    float phase = 0.0f; /* 0–1 */

    while (1)
    {
        /* triangle LFO 0→1→0, 1 Hz (100 steps × 10 ms) */
        float v = (phase < 0.5f) ? phase * 2.0f : (1.0f - phase) * 2.0f;
        build_osc_packet(pkt, v);
        sendto(s, pkt, sizeof(pkt), 0,
               (struct sockaddr *)&dst, sizeof(dst));

        phase += 0.01f;
        if (phase >= 1.0f)
            phase -= 1.0f;

        //DO 1000 if you want iperf to work
        vTaskDelay(pdMS_TO_TICKS(10)); /* 10 ms */
        //vTaskDelay(pdMS_TO_TICKS(1000)); /* 10 ms */
    }
}

#define ECHO_HOST_IP "10.0.0.203"
#define ECHO_PORT 9000

static void udp_ping_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in dst = {
        .sin_family = AF_INET,
        .sin_port = htons(ECHO_PORT)};
    inet_aton(ECHO_HOST_IP, &dst.sin_addr);

    uint32_t seq = 0, t0, t1;
    char buf[32];

    while (1)
    {
        seq++;
        t0 = (uint32_t)esp_timer_get_time(); /* µs since boot */
        memcpy(buf, &seq, 4);
        memcpy(buf + 4, &t0, 4);
        sendto(sock, buf, 8, 0,
               (struct sockaddr *)&dst, sizeof(dst));

        struct sockaddr_in src;
        socklen_t sl = sizeof(src);
        int n = recvfrom(sock, buf, sizeof(buf), 0,
                         (struct sockaddr *)&src, &sl);
        if (n == 8 && !memcmp(&seq, buf, 4))
        {
            memcpy(&t0, buf + 4, 4);
            t1 = esp_timer_get_time();
            printf("RTT %lu: %.2f ms\n",
                   (unsigned long)seq,
                   (t1 - t0) / 1000.0f);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}