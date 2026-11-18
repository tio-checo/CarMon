/*********************************************************************
 *
 * Wi-Fi station connection.
 *
 *********************************************************************/

#ifndef __WIFI_H
#define __WIFI_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/*
 * Initialize station and start connecting to specified network.
 * Driver copies credentials before returning, without saving them to NVS.
 * Call once, from a task. Password must contain 8 to 63 bytes.
 */
extern esp_err_t wifi_init(const char *, const char *);

/* TRUE when station has an IPv4 address; these queries do not block. */
extern bool wifi_is_connected(void);

/*
 * Changes whenever connection is lost or an address is obtained.
 * Allows a transport to detect even a disconnect followed by a quick reconnect.
 */
extern uint32_t wifi_get_generation(void);

#endif /* __WIFI_H */
