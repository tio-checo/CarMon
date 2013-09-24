#ifndef __MON_H
#define __MON_H

/* constants */
#define MON_DIAG_MODE_BT	'b'	/* Bluetooth used as diag device */
#define MON_DIAG_MODE_SER	's'	/* UART used as diag device */

/* initialize monitor module */
extern int mon_init(void);

/* Enter diagnostic mode. */
extern int mon_diag_enter(unsigned char);

/* Leave diagnostic mode. */
extern int mon_diag_leave(void);

/* Return TRUE if running in diagnostic mode. Otherwise returns FALSE. */
extern BOOL mon_is_diag_mode(void);

#endif /* __MON_H */
