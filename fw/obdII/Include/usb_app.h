/*********************************************************************
 *
 * USB application layer.
 *
 *********************************************************************/

#ifndef __USB_APP_H
#define __USB_APP_H

#include <GenericTypeDefs.h>

/* Initialization routine */
extern int uapp_init(void);

/* USB application service routine. */
extern int uapp_svc(void);

/* CDC ACM getchar routine */
extern int usbsacm_getchar(unsigned char *);

/* CDC ACM putchar routine */
extern int usbsacm_putchar(unsigned char, BOOL);

/*
 *  CDC ACM putchar routine - sends the whole packet at once
 *
 * Sends the whole packet at once in order to utilize available bandwidth.
 * If the packet does not fit one buffer, it is fragmented.
 * Returns EAGAIN if there is no space in the transmit buffer.
 * Returns 0 if character has been queued for transmission.
 */
extern int usbsacm_putchars(unsigned char, BOOL);

#endif /* __USB_APP_H */

