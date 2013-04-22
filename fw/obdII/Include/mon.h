#ifndef __MON_H
#define __MON_H

/* initialize monitor module */
extern int mon_init(void);

/* Enter diagnostic mode. */
extern int mon_diag_enter(void);

/* Return TRUE if running in diagnostic mode. Otherwise returns FALSE. */
extern BOOL mon_is_diag_mode(void);

#endif /* __MON_H */
