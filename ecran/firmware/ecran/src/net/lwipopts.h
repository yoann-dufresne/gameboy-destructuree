/**
 * lwIP — configuration du module ÉCRAN
 *
 * Dérivé de pico-examples/pico_w/wifi/lwipopts_examples_common.h, avec trois
 * écarts qui comptent pour la latence (plan §5, phase 2) :
 *
 *   - PBUF_POOL_SIZE et MEM_SIZE relevés : un pool à sec se traduit par des
 *     pertes silencieuses, exactement ce qu'on ne veut pas diagnostiquer plus tard ;
 *   - API raw uniquement, pas de sockets ni de netconn : moins de copies mémoire
 *     et pas de réveils inutiles ;
 *   - toute trace désactivée, y compris en build de debug.
 */
#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define MEM_LIBC_MALLOC             0
#define MEM_ALIGNMENT               4

/* Dimensionnement pour du flux UDP soutenu. */
#define MEM_SIZE                    16000
#define PBUF_POOL_SIZE                 48
#define MEMP_NUM_UDP_PCB                8
#define MEMP_NUM_ARP_QUEUE             10
#define MEMP_NUM_TCP_SEG               32

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_UDP                    1
#define LWIP_TCP                    1 /* conservé : la pile du SDK s'y attend */
#define LWIP_DNS                    0 /* aucune résolution en régime établi */
#define LWIP_DHCP                   1

#define TCP_MSS                     1460
#define TCP_WND                     (4 * TCP_MSS)
#define TCP_SND_BUF                 (4 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))

#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_TX_SINGLE_PBUF   1
#define LWIP_CHKSUM_ALGORITHM       3

#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

#define MEM_STATS                   0
#define SYS_STATS                   0
#define MEMP_STATS                  0
#define LINK_STATS                  0
#define LWIP_STATS                  0
#define LWIP_DEBUG                  0

#endif /* _LWIPOPTS_H */
