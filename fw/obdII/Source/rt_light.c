/*********************************************************************
 *
 * Support for light real time system.
 *
 *********************************************************************/

#include "HardwareProfile.h"
#include "xc.h"

#include "bt.h"
#include "data_acq.h"
#include "kwp2k.h"
#include "master_comm.h"
#include "mon.h"
#include "obd.h"
#include "rt_light.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

/* constants */
#define RTL_TIMER_POOL_SIZE	16	/* number of available timers */
#define RTL_CV_POOL_SIZE	16	/* number of available cond vars */

/* local variables */
/* heartbeat LED blinking mode */
static unsigned char heartbeat_mode = HB_MODE_TICKS;

/* pool of timers */
static rtl_timer_t rtl_pool_timers[RTL_TIMER_POOL_SIZE];

/* pool of conditional variables */
static rtl_cv_t rtl_pool_cvs[RTL_CV_POOL_SIZE];

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
 * handle hartbeat LED
 */
static void
rtl_heartbeat_svc(void)
{
	static int hb_counter = 250000 / RTL_SERVICE_PERIOD;
	static int hb_num_ticks = 1;
	static int hb_curr_tick = 1;

	/* indicate if BT module is currently paired */
	if (bt_is_paired()) {
		heartbeat_mode = HB_MODE_TICKS;

		/*
		 * Add one more tick if communication with ECU has been
		 * established.
		 */
		if (kwp2k_comm_is_established())
			hb_num_ticks = 2;
		else
			hb_num_ticks = 1;
	} else {
		heartbeat_mode = HB_MODE_FIFTY_FIFTY;
	}

	switch (heartbeat_mode) {
	case HB_MODE_FIFTY_FIFTY:
		if (--hb_counter <= 0) {
			hb_counter = 250000 / RTL_SERVICE_PERIOD;
			LATBINV = 1 << 13;
		}
		break;

	case HB_MODE_TICKS:
		if (hb_curr_tick > 0) {
			hb_counter--;
			if (hb_counter <= 0) {
				hb_curr_tick--;
				if (hb_curr_tick > 0)
					hb_counter =
					    200000 / RTL_SERVICE_PERIOD;
				else
					hb_counter =
					    1000000 / RTL_SERVICE_PERIOD;
			} else if (hb_counter <= 175000 / RTL_SERVICE_PERIOD) {
				LATBSET = 1 << 13;
			} else {
				LATBCLR = 1 << 13;
			}
		} else {
			/* silent period */
			if (--hb_counter <= 0) {
				hb_counter = 200000 / RTL_SERVICE_PERIOD;
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

	/*
	 * Timer 1 used to generate interrupt for main service loop.
	 * Main service loop is called every RTL_SERVICE_PERIOD us.
	 */
	T1CON = 0;
	TMR1 = 0;
	PR1 = (GetPeripheralClock() / 1000000) * RTL_SERVICE_PERIOD;

	/* clear Timer 1 interrupt flag */
	IFS0CLR = 1 << 4;

	/* set priority 1, sub-priority 0 */
	IPC1CLR = 0x1f;
	IPC1SET = 0x04;

	/* enable Timer 1 interrupt */
	IEC0SET = 1 << 4;

	/* Enable Timer 1 */
	T1CONSET = 0x8000;

	return 0;
}

/*
 * Suspend the module
 */
int
rtl_suspend(void)
{
	/* Stop timer 1 */
	T1CONCLR = 0x8000;

	return 0;
}

/*
 * Timer 1 interrupt handler
 */
void
__attribute__ ((interrupt)) isr_timer1(void)
{
/*	LATBCLR = 1 << 12;	done in interrupt prolog */

	if (mon_is_diag_mode()) {
		/*
		 * Diagnostic mode. 
		 */
		mon_svc();
	} else {
		kwp2k_svc();
		obd_svc();
		daq_svc();

		/*
		 * Must be located after kwp2k_svc() and obd_svc() state
		 * machines, sice it's role is to process KWP2K packets received
		 * from ECU only if obd_svc() does not take care of it.
		 */
		mcom_svc();
		rtl_timer_svc();
	}

	/* Services which run in both normal and diagnostic modes */
	bt_svc();
	rtl_heartbeat_svc();

	/* clear Timer 1 interrupt flag */
	IFS0CLR = 1 << 4;

	/* turn off LED indicating CPU load */
	LATBSET = 1 << 12;
}
