#ifndef __BT_H
#define __BT_H

#include "GenericTypeDefs.h"

/* return codes */
#define BT_OK		0	/* operation succeeded */
#define BT_RX_EMPTY	-1	/* receive buffer empty */
#define BT_TX_FULL	-2	/* transmit buffer full */

/* initialize Bluetooth module */
extern int bt_init(void);

/* Suspend Bluetooth module */
extern int bt_suspend(void);

extern int bt_putchar(unsigned char ch);
extern int bt_getchar(unsigned char *ch);

/* Return TRUE if BT module is paired with master, otherwise return FALSE */
extern BOOL bt_is_paired(void);

/* main service routine */
extern int bt_service(void);
#endif /* __BT_H */
