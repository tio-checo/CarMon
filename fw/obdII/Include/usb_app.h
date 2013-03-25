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

/* Main service routine - called periodically from timer interrupt. */
extern int uapp_svc(void);

#endif /* __USB_APP_H */

