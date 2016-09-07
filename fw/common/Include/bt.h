/*********************************************************************
 *
 * Provides API and services for BTM-112 Bluetooth module.
 *
 *********************************************************************/

#ifndef __BT_H
#define __BT_H

#include "GenericTypeDefs.h"

/* initialize Bluetooth module */
extern int bt_init(void);

/* Suspend Bluetooth module */
extern int bt_suspend(void);

extern int bt_putchar(unsigned char, BOOL);
extern int bt_getchar_gps(unsigned char *);
extern int bt_getchar_master(unsigned char *);

/*
 * Return TRUE if BT module is paired with remote BT end-point;
 * FALSE otherwise.
 */
extern BOOL bt_is_paired(void);
/*
 * Return TRUE if BT module is paired with GPS module;
 * FALSE otherwise.
 */
extern BOOL bt_is_paired_with_gps(void);

/*
 * Return TRUE if BT module is paired with master module;
 * FALSE otherwise.
 */
extern BOOL bt_is_paired_with_master(void);

#endif /* __BT_H */
