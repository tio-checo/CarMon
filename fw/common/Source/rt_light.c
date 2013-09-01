/*********************************************************************
 *
 * Support for light real time system.
 *
 *********************************************************************/

#include "HardwareProfile.h"
#include "xc.h"

#include "bt.h"
#include "data_acq.h"

#ifdef KWP2K_MODULE
#include "kwp2k.h"
#endif

#include "master_comm.h"
#include "mon.h"
#include "pic_core.h"
#include "rt_light.h"
#include "usb_app.h"
#include "utils.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* constants */
#define RTL_TIMER_POOL_SIZE	16	/* # of available timers */
#define RTL_CV_POOL_SIZE	16	/* # of available cond vars */
#define RTL_SVC_POOL_SIZE	16	/* # of available services */
#define RTL_ISTAT_NUM		1	/* # of tracked interrupt statistics */
#define RTL_ISTAT_TIMER1	0	/* index of Timer 1 int. statistic */

extern int bt_svc(void);

/* types */
/* service descriptor */
typedef struct rtl_svc {
	const char	*name;		/* service name */
	int		(*svc)(void);	/* service method */
	BOOL		enabled;	/* service enabled */
	BOOL		run_in_diag;	/* service enabled in diagnostic mode */
} rtl_svc_t;

/* local variables */
/* heartbeat LED blinking mode */
static unsigned char heartbeat_mode = HB_MODE_TICKS;

/* pool of timers */
static rtl_timer_t rtl_pool_timers[RTL_TIMER_POOL_SIZE];

/* pool of conditional variables */
static rtl_cv_t rtl_pool_cvs[RTL_CV_POOL_SIZE];

/* pool of service descriptors */
static rtl_svc_t rtl_pool_svcs[RTL_SVC_POOL_SIZE];

/* interrupt statistics */
static struct rtl_istat_t {
	char	*name;		/* Interrupt description */
	int	num;		/* Interrupt number */
	unsigned long	min_time;	/* Min timer ticks spent in handler */
	unsigned long	max_time;	/* Max timer ticks spent in handler */
} rtl_istat[RTL_ISTAT_NUM];

/* local functions */
/*
 * Timer service routine.
 * Decrements currently active timers.
 */
static int
rtl_timer_svc(void)
{
	int i;

	/*
	 * Handle timers - stop when the first unallocated is found.
	 */
	for (i = 0; i < RTL_TIMER_POOL_SIZE && rtl_pool_timers[i].allocated;
	    i++) {
		if (rtl_pool_timers[i].tout != 0)
			rtl_pool_timers[i].tout--;
	}

	return	0;
}

/*
 * handle heartbeat LED
 */
static void
rtl_heartbeat_svc(void)
{
	static int hb_counter = RTL_TIME_250_MSEC;
	static int hb_num_ticks = 1;
	static int hb_curr_tick = 1;

	/* indicate if BT module is currently paired */
	if (bt_is_paired()) {
		heartbeat_mode = HB_MODE_TICKS;

#ifdef KWP2K_MODULE
		/*
		 * Add one more tick if communication with ECU has been
		 * established.
		 */
		if (kwp2k_comm_is_established())
			hb_num_ticks = 2;
		else
#endif
			hb_num_ticks = 1;
	} else {
		heartbeat_mode = HB_MODE_FIFTY_FIFTY;
	}

	switch (heartbeat_mode) {
	case HB_MODE_FIFTY_FIFTY:
		if (--hb_counter <= 0) {
			hb_counter = RTL_TIME_250_MSEC;
			LATBINV = 1 << 13;
		}
		break;

	case HB_MODE_TICKS:
		if (hb_curr_tick > 0) {
			hb_counter--;
			if (hb_counter <= 0) {
				hb_curr_tick--;
				if (hb_curr_tick > 0)
					hb_counter = RTL_TIME_200_MSEC;
				else
					hb_counter = RTL_TIME_1_SEC;
			} else if (hb_counter <= RTL_TIME_175_MSEC) {
				LATBSET = 1 << 13;
			} else {
				LATBCLR = 1 << 13;
			}
		} else {
			/* silent period */
			if (--hb_counter <= 0) {
				hb_counter = RTL_TIME_200_MSEC;
				hb_curr_tick = hb_num_ticks;
				LATBCLR = 1 << 13;
			}
		}
		break;
	}
}

/* public functions */
/*
 * Allocate timer from pool and initialize it.
 * If timer has not been allocated yet (*ptimer is NULL), then try to allocate
 * one from pool of timers. If there is no free timer available, return ENOSPC.
 * Otherwise, initialize pointer to the timer allocated.
 *
 * Timeout specified in microseconds.
 */
int
rtl_timer_init(rtl_timer_t **ptimer, rtl_time_t tout)
{
	int	i;

	/* timer not allocated yet, try to allocate one from available pool */
	if (*ptimer == NULL) {
		for (i = 0;
		    i < RTL_TIMER_POOL_SIZE && rtl_pool_timers[i].allocated;
		    i++)

		/* no timer available, return */
		if (i == RTL_TIMER_POOL_SIZE)
			return ENOSPC;

		*ptimer = &rtl_pool_timers[i];
	}

	/* mark timer allocated */
	(*ptimer)->allocated = TRUE;
	(*ptimer)->tout = tout / RTL_SERVICE_PERIOD;

	return 0;
}

/*
 * Return TRUE if timer expired, otherwise return FALSE.
 */
BOOL
rtl_timer_expired(rtl_timer_t *ptimer)
{
	return (ptimer->tout == 0);
}

/*
 * Allocate condition variable from pool and initialize it.
 * If there is no free condition variable available, return ENOSPC.
 * Otherwise, initialize pointer to condition variable allocated.
 *
 * Timeout specified in microseconds.
 */
int
rtl_cond_init(rtl_cv_t **pcv, const char *desc)
{
	int i;

	for (i = 0; i < RTL_CV_POOL_SIZE && rtl_pool_cvs[i].allocated; i++);

	/* no condition variable available, return */
	if (i == RTL_CV_POOL_SIZE)
		return ENOSPC;

	*pcv = &rtl_pool_cvs[i];

	/* mark cv allocated */
	(*pcv)->allocated = TRUE;

	/* initialize description field to given string */
	(*pcv)->description = desc;

	return 0;
}

/*
 * Initialize service. Return -1 in case of failure, otherwise return
 * service handle.
 */
rtl_svc_handle_t
rtl_svc_init(const char *name, int (*svc)(void), BOOL en, BOOL run_in_diag)
{
	int	i;

	/* Find free slot in service table. */
	for (i = 0; i < RTL_SVC_POOL_SIZE && rtl_pool_svcs[i].svc != NULL; i++)
		;

	/* Service table is full, return. */
	if (i == RTL_SVC_POOL_SIZE)
		return -1;

	/* Store service in the table and return service handle. */
	/*
	 * First initialize pointer to service routine, then enable it.
	 * In such case we don't need to disable service routine invoked
	 * from interrupt handler when manipulating service table.
	 */
	rtl_pool_svcs[i].name = name;
	rtl_pool_svcs[i].svc = svc;
	rtl_pool_svcs[i].enabled = en;
	rtl_pool_svcs[i].run_in_diag = run_in_diag;

	return (rtl_svc_handle_t) i;
}

/*
 * Enable service.
 */
int
rtl_svc_enable(rtl_svc_handle_t sh)
{
	/* Validate service handle */
	if (sh < 0 || sh >= RTL_SVC_POOL_SIZE || rtl_pool_svcs[sh].svc == NULL)
		return EINVAL;

	rtl_pool_svcs[sh].enabled = TRUE;

	return 0;
}

/*
 * Disable service.
 */
int
rtl_svc_disable(rtl_svc_handle_t sh)
{
	/* Validate service handle */
	if (sh < 0 || sh >= RTL_SVC_POOL_SIZE || rtl_pool_svcs[sh].svc == NULL)
		return EINVAL;

	rtl_pool_svcs[sh].enabled = FALSE;

	return 0;
}

/*
 * Initialize "Real Time Light" (RTL) module.
 */
int
rtl_init(void)
{
	int i;

	/* Init pool of timers - mark all timers as available */
	for (i = 0; i < RTL_TIMER_POOL_SIZE; i++)
		rtl_pool_timers[i].allocated = FALSE;

	/* Init pool of condition variables - all cvs are available */
	for (i = 0; i < RTL_CV_POOL_SIZE; i++) {
		rtl_pool_cvs[i].allocated = FALSE;
		rtl_pool_cvs[i].condition = FALSE;
		rtl_pool_cvs[i].ptimer = NULL;
		rtl_pool_cvs[i].description = "free cv";
	}

	/* Init pool of services - mark all services available */
	for (i = 0; i < RTL_SVC_POOL_SIZE; i++) {
		rtl_pool_svcs[i].svc = NULL;
		rtl_pool_svcs[i].enabled = FALSE;
		rtl_pool_svcs[i].run_in_diag = FALSE;
	}

	/* Initialize interrupt statistics */
	rtl_istat[RTL_ISTAT_TIMER1].name = "Timer1";
	rtl_istat[RTL_ISTAT_TIMER1].num = _TIMER_1_IRQ;
	rtl_istat[RTL_ISTAT_TIMER1].min_time = RTL_SERVICE_PERIOD * 1000L;
	rtl_istat[RTL_ISTAT_TIMER1].max_time = 0L;

	/*
	 * Timer 1 used to generate interrupt for main service loop.
	 * Main service loop is called every RTL_SERVICE_PERIOD us.
	 */
	T1CON = 0;
	TMR1 = 0;
	PR1 = (GetPeripheralClock() / 1000000) * RTL_SERVICE_PERIOD - 1;

	/* clear Timer 1 interrupt flag */
	IFS0CLR = 1 << 4;

	/* set priority 1, sub-priority 0 */
	IPC1CLR = 0x1f;
	IPC1SET = 0x04;

	/* enable Timer 1 interrupt */
	IEC0SET = 1 << 4;

	return 0;
}

/*
 * Enable service handler.
 */
int
rtl_svc_handler_enable(void)
{
	/* Start Timer 1 */
	T1CONSET = 0x8000;

	return 0;
}

/*
 * Disable service handler.
 */
int
rtl_svc_handler_disable(void)
{
	/* Stop Timer 1 */
	T1CONCLR = 0x8000;

	return 0;
}

/*
 * Display service table and statistics for debugging purposes.
 */
int
rtl_svc_display_table(int (*print_str)(const char *))
{
	char s[81];
	int i;

	/* print header */
	print_str("-------------------------------------\n\r");
	print_str("SH ADDRESS     E  D  N\n\r");
	print_str("-------------------------------------\n\r");

	for (i = 0; (i < RTL_SVC_POOL_SIZE) && (rtl_pool_svcs[i].svc != NULL);
	    i++) {
		snprintf(s, sizeof (s), "%2d 0x%08X  %s  %s  %s\n\r",
		    i, (unsigned int) rtl_pool_svcs[i].svc,
		    rtl_pool_svcs[i].enabled ? "Y" : "N",
		    rtl_pool_svcs[i].run_in_diag ? "Y" : "N",
		    rtl_pool_svcs[i].name);

		print_str(s);
	}

	print_str("-------------------------------------\n\r");
	print_str("IRQ# min[ticks] max[ticks] NAME\n\r");
	print_str("-------------------------------------\n\r");

	for (i = 0; i < RTL_ISTAT_NUM; i++) {
		snprintf(s, sizeof (s), "%4d %10ld %10ld %s\n\r",
		    rtl_istat[0].num, rtl_istat[0].min_time,
		    rtl_istat[0].max_time, rtl_istat[0].name);
		print_str(s);
	}

	print_str("-------------------------------------\n\r");

	return 0;
}

/*
 * Suspend the module
 */
int
rtl_suspend(void)
{
	/* Disable service handler. */
	rtl_svc_handler_disable();

	return 0;
}

/*
 * Timer 1 interrupt handler
 */
void
__attribute__ ((interrupt)) isr_timer1(void)
{
	int i;
	unsigned long t;

/*	LATBCLR = 1 << 12;	done in interrupt prolog */

	/* Handle timers */
	rtl_timer_svc();
	/* handle heartbeat LED */
	rtl_heartbeat_svc();

	/* Run enabled services */
	for (i = 0; (i < RTL_SVC_POOL_SIZE) && (rtl_pool_svcs[i].svc != NULL);
	    i++) {
		/*
		 * If in diagnostic mode, make sure that particular service
		 * should run in that mode.
		 */
		if (rtl_pool_svcs[i].enabled) {
			if (!mon_is_diag_mode()) {
				rtl_pool_svcs[i].svc();
			} else if (rtl_pool_svcs[i].run_in_diag) {
				rtl_pool_svcs[i].svc();
			}
		}
	}

	/* Record time spent in this handler */
	t = TMR1;
	if (rtl_istat[RTL_ISTAT_TIMER1].min_time > t)
		rtl_istat[RTL_ISTAT_TIMER1].min_time = t;

	if (rtl_istat[RTL_ISTAT_TIMER1].max_time < t)
		rtl_istat[RTL_ISTAT_TIMER1].max_time = t;

	/* clear Timer 1 interrupt flag */
	IFS0CLR = 1 << 4;

	/* turn off LED indicating CPU load */
	LATBSET = 1 << 12;
}
