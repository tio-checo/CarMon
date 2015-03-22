#ifndef __RTL_H
#define __RTL_H

#include "GenericTypeDefs.h"

/* period of calling main service loop in us */
#define RTL_SERVICE_PERIOD	100

/* heartbeat modes */
#define HB_MODE_OFF		0
#define HB_MODE_ON		1
#define HB_MODE_FIFTY_FIFTY	2
#define HB_MODE_TICKS		3

/* macros */
/* Converts time in [ms] to RTL ticks. */
#define	RTL_TIME_MS2RTICKS(x)	(((x) * 1000) / RTL_SERVICE_PERIOD)

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

/* service handle */
typedef int rtl_svc_handle_t;

/* public functions */
extern int rtl_init(void);

/* Suspend the module */
extern int rtl_suspend(void);

/* timers */
extern int rtl_timer_init(rtl_timer_t **, rtl_time_t);
extern BOOL rtl_timer_expired(rtl_timer_t *);

/* services */
/*
 * Initialize service. Return -1 in case of failure, otherwise return
 * service handle.
 */
extern rtl_svc_handle_t rtl_svc_init(const char *, int (*)(), BOOL, BOOL);
/* Enable service. */
extern int rtl_svc_enable(rtl_svc_handle_t);
/* Disable service. */
extern int rtl_svc_disable(rtl_svc_handle_t);
/* Enable service handler. */
extern int rtl_svc_handler_enable(void);
/* Disable service handler. */
extern int rtl_svc_handler_disable(void);
/* Display service table for debugging purposes. */
extern int rtl_svc_display_table(int (*)(const char *));
/* Configure heartbeat LED. */
extern int rtl_hb_mode(int, int, int);

#endif /* __RTL_H */
