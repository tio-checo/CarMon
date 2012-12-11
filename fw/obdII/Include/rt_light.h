#ifndef __RTL_H
#define __RTL_H

#include "GenericTypeDefs.h"

/* period of calling main service loop  in us */
#define RTL_SERVICE_PERIOD	100

/* heartbeat modes */
#define HB_MODE_FIFTY_FIFTY	0
#define HB_MODE_TICKS		1

/* types */
/* time */
typedef unsigned long rtl_time_t;
/* timer */
typedef struct _rtl_timer_t {
	BOOL		allocated;
	rtl_time_t	tout;
} rtl_timer_t;

/* condition variable */
typedef struct _rtl_cv_t {
	BOOL		allocated;
	BOOL		condition;
	rtl_timer_t	*ptimer;
	const char	*description;
} rtl_cv_t;

/* public functions */
extern int rtl_init(void);

/* Suspend the module */
extern int rtl_suspend(void);

/* timers */
extern int rtl_timer_init(rtl_timer_t **, rtl_time_t);
extern BOOL rtl_timer_expired(rtl_timer_t *);

#endif /* __RTL_H */
