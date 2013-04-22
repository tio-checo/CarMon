#ifndef __BT_H
#define __BT_H

#include "GenericTypeDefs.h"

/* initialize Bluetooth module */
extern int bt_init(void);

/* Suspend Bluetooth module */
extern int bt_suspend(void);

extern int bt_putchar(unsigned char ch);
extern int bt_getchar(unsigned char *ch);

/* Return TRUE if BT module is paired with master, otherwise return FALSE */
extern BOOL bt_is_paired(void);

#endif /* __BT_H */
