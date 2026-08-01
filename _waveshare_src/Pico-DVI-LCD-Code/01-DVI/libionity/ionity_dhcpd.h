#ifndef IONITY_DHCPD_H
#define IONITY_DHCPD_H

#include "pico/stdlib.h"

#ifdef __cplusplus
extern "C" {
#endif

/* DHCP + wildcard-DNS for the provisioning AP. Start after
 * cyw43_arch_enable_ap_mode(); clients get 192.168.4.16-23. */
bool ionity_dhcpd_start(void);
void ionity_dhcpd_stop(void);

#ifdef __cplusplus
}
#endif

#endif
