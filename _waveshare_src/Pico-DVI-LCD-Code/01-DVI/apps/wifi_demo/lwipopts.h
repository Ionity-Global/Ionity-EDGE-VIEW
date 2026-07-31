#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// Generally settings for Pico W WiFi with lwIP (NO_SYS=1 mode)

// NO_SYS=1: use raw API, no OS needed
#define NO_SYS                      1
#define NO_SYS_NO_TIMERS            0

// Memory
#define MEM_SIZE                    (16 * 1024)
#define MEMP_NUM_PBUF               16
#define MEMP_NUM_UDP_PCB            8
#define MEMP_NUM_TCP_PCB            8
#define MEMP_NUM_TCP_PCB_LISTEN     4
#define MEMP_NUM_TCP_SEG            16
#define MEMP_NUM_SYS_TIMEOUT        10
#define PBUF_POOL_SIZE              16
#define PBUF_POOL_BUFSIZE           1500

// TCP
#define TCP_MSS                     (1500 - 40)
#define TCP_SND_BUF                 (4 * TCP_MSS)
#define TCP_SND_QUEUELEN            (2 * TCP_SND_BUF / TCP_MSS)
#define TCP_WND                     (4 * TCP_MSS)
#define TCP_LISTEN_BACKLOG          1

// DHCP
#define LWIP_DHCP                   1
#define LWIP_DNS                    1

// Checksums (hardware offload not available on Pico)
#define CHECKSUM_BY_HARDWARE        0
#define LWIP_CHECKSUM_CTRL_PER_NETIF 0

// ARP
#define LWIP_ARP                    1

// ICMP
#define LWIP_ICMP                   1

// UDP
#define LWIP_UDP                    1

// Stats (disable for smaller binary)
#define LWIP_STATS                  0

// Netconn & Socket APIs (disabled for NO_SYS)
#define LWIP_NETCONN                0
#define LWIP_SOCKET                 0

// printf (for lwIP debug)
#define LWIP_DEBUG                  0

#endif
