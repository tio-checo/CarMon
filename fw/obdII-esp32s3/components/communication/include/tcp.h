/*********************************************************************
 *
 * Single-client TCP transport and RTL FIFO interface.
 *
 *********************************************************************/

#ifndef __TCP_H
#define __TCP_H

#include <stdint.h>
#include "GenericTypeDefs.h"

#include "esp_err.h"

/*
 * Start the server task once. It waits for Wi-Fi and recreates the listener
 * after a loss of connection. ESP_OK means task started, not yet listening.
 */
extern esp_err_t tcp_server_init(uint16_t);

/* RTL-only, nonblocking character interface (same convention as bt.c). */
extern int tcp_getchar(unsigned char *);
extern int tcp_putchar(unsigned char, BOOL);
extern BOOL tcp_is_connected(void);

/* RTL resets protocol state before acknowledging a new TCP session. */
extern BOOL tcp_session_changed(void);
extern void tcp_session_ack(void);

#endif /* __TCP_H */
