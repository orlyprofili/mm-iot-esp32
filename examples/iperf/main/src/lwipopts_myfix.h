// UNUSED
/*  components/lwip/include/lwipopts_myfix.h
 *
 *  Force software ICMP checksums on any netif that *doesn't* say otherwise.
 *  Works around MM6108 off-load bug (echo replies had checksum = 0).
 */
#define LWIP_CHECKSUM_CTRL_PER_NETIF   1
#define CHECKSUM_CHECK_ICMP            0     /* accept zero-checksum frames   */
#define CHECKSUM_GEN_ICMP              1     /* generate checksum in software */
