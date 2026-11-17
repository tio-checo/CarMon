#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "carmon_platform.h"

/* Registered services are not yet IRAM-safe. Do not claim cache-off operation. */
#if CONFIG_GPTIMER_ISR_CACHE_SAFE
#error "RTL service graph is not IRAM-safe; disable CONFIG_GPTIMER_ISR_CACHE_SAFE"
#endif
static gptimer_handle_t rtl_gptimer;
static bool rtl_timer_running;
static int hb_level = 1;
static bool rtl_on_alarm(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *);
/*********************************************************************
 *
 * Support for light real time system.
 *
 *********************************************************************/


#include "bt.h"

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
	long		start_time;	/* max start time */
	long		svc_time;	/* max time spent in the service */
} rtl_svc_t;

/* macros */
/* turn heartbeat LED on */
#define HB_LED_ON gpio_set_level(CARMON_GPIO_HEARTBEAT, hb_level = 0)
/* turn heartbeat LED off */
#define HB_LED_OFF gpio_set_level(CARMON_GPIO_HEARTBEAT, hb_level = 1)
/* toggle heartbeat LED */
#define HB_LED_TOGGLE gpio_set_level(CARMON_GPIO_HEARTBEAT, hb_level ^= 1)

/* local variables */
/* heartbeat LED blinking mode */
static unsigned char hb_mode;
/* heartbeat LED period */
static int hb_period;
/* number of flashes during HB_MODE_TICKS mode */  
static int hb_num_ticks;

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
	unsigned long	prlg_time;	/* Max timer ticks spent in prologue */
	unsigned long	min_time;	/* Min timer ticks spent in handler */
	unsigned long	max_time;	/* Max timer ticks spent in handler */
	unsigned long	missed;	/* Number of missed interrupts */
	unsigned long	nloops;	/* Max number of loops spent in ISR. */
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
	static int hb_counter = RTL_TIME_MS2RTICKS(250);
	static int hb_curr_tick = 1;

	switch (hb_mode) {
	/* LED OFF. */
	case HB_MODE_OFF:
		HB_LED_OFF;
		break;

	/* LED always ON. */
	case HB_MODE_ON:
		HB_LED_ON;
		break;

	case HB_MODE_FIFTY_FIFTY:
		if (--hb_counter <= 0) {
			hb_counter = hb_period / 2;
			HB_LED_TOGGLE;
		}
		break;

	case HB_MODE_TICKS:
		if (hb_curr_tick > 0) {
			hb_counter--;
			if (hb_counter <= 0) {
				hb_curr_tick--;
				if (hb_curr_tick > 0) {
					/* Another flash to be commenced. */
					hb_counter = RTL_TIME_MS2RTICKS(200);

					HB_LED_ON;
				} else {
					/*
					 * Done with flashes for this burst.
					 * Now remain silent for a while.
					 */
					hb_counter = hb_period;
				}
			} else if (hb_counter <= RTL_TIME_MS2RTICKS(175)) {
				/* Flash ended, turn the LED off. */
				HB_LED_OFF;
			}
		} else {
			/* Period of silence */
			if (--hb_counter <= 0) {
				/*
				 * Silent period ended,
				 * kick off new burst.
				 */
				hb_counter = RTL_TIME_MS2RTICKS(200);
				hb_curr_tick = hb_num_ticks;

				HB_LED_ON;
			}
		}
		break;
	}
}

/* public functions */
/*
 * Configure heartbeat LED.
 */
int
rtl_hb_mode(int mode, int arg1, int arg2)
{
	hb_mode = mode;

	switch (mode) {
	case HB_MODE_FIFTY_FIFTY:
		hb_period = RTL_TIME_MS2RTICKS(arg1);
		break;

	case HB_MODE_TICKS:
		hb_period = RTL_TIME_MS2RTICKS(arg1);
		hb_num_ticks = arg2;
		break;

	case HB_MODE_ON:
		break;
	}

	return 0;
}

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
			;

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
	rtl_pool_svcs[i].start_time = rtl_pool_svcs[i].svc_time = 0;

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
	if (xPortGetCoreID() != 1 || rtl_gptimer != NULL) return EINVAL;

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

	/* Initialize heartbeat LED */
	rtl_hb_mode(HB_MODE_FIFTY_FIFTY, 500, 0);

	/* Initialize interrupt statistics */
	rtl_istat[RTL_ISTAT_TIMER1].name = "GPTimer";
	rtl_istat[RTL_ISTAT_TIMER1].num = 0;
	rtl_istat[RTL_ISTAT_TIMER1].prlg_time = 0L;
	rtl_istat[RTL_ISTAT_TIMER1].min_time = RTL_SERVICE_PERIOD;
	rtl_istat[RTL_ISTAT_TIMER1].max_time = 0L;
	rtl_istat[RTL_ISTAT_TIMER1].missed = 0L;
	rtl_istat[RTL_ISTAT_TIMER1].nloops = 0L;

    gptimer_config_t cfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
        .intr_priority = 1,
    };
    gptimer_alarm_config_t alarm = {
        .alarm_count = RTL_SERVICE_PERIOD,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    gptimer_event_callbacks_t callbacks = { .on_alarm = rtl_on_alarm };
    ESP_ERROR_CHECK(gptimer_new_timer(&cfg, &rtl_gptimer));
    ESP_ERROR_CHECK(gptimer_set_alarm_action(rtl_gptimer, &alarm));
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(rtl_gptimer, &callbacks, NULL));
    ESP_ERROR_CHECK(gptimer_enable(rtl_gptimer));

	return 0;
}

/*
 * Enable service handler.
 */
int
rtl_svc_handler_enable(void)
{
    if (!rtl_gptimer || xPortGetCoreID() != 1) return EINVAL;
    if (!rtl_timer_running) {
        ESP_ERROR_CHECK(gptimer_start(rtl_gptimer));
        rtl_timer_running = true;
    }
    return 0;
}

/*
 * Disable service handler.
 */
int
rtl_svc_handler_disable(void)
{
    if (!rtl_gptimer || xPortGetCoreID() != 1) return EINVAL;
    if (rtl_timer_running) {
        ESP_ERROR_CHECK(gptimer_stop(rtl_gptimer));
        rtl_timer_running = false;
    }
    return 0;
}

/*
 * Clear kstats.
 */
int
rtl_svc_clear_kstats(void)
{
	int i;

	/* Service kstats. */
	for (i = 0; (i < RTL_SVC_POOL_SIZE) && (rtl_pool_svcs[i].svc != NULL);
	    i++) {
		rtl_pool_svcs[i].start_time = rtl_pool_svcs[i].svc_time = 0;
	}

	/* Interrupt kstats. */
	rtl_istat[RTL_ISTAT_TIMER1].prlg_time = 0L;
	rtl_istat[RTL_ISTAT_TIMER1].min_time = RTL_SERVICE_PERIOD;
	rtl_istat[RTL_ISTAT_TIMER1].max_time = 0L;
	rtl_istat[RTL_ISTAT_TIMER1].missed = 0L;
	rtl_istat[RTL_ISTAT_TIMER1].nloops = 0L;

	return 0;
}

/*
 * Display service table and statistics for debugging purposes.
 */
int
rtl_svc_display_kstats(int (*print_str)(const char *))
{
	char s[81];
	int i;

	/* print header */
	print_str("---------------------------------------------------------\n\r");
	print_str("SH ADDRESS    E D start_time svc_time N\n\r");
	print_str("---------------------------------------------------------\n\r");

	for (i = 0; (i < RTL_SVC_POOL_SIZE) && (rtl_pool_svcs[i].svc != NULL);
	    i++) {
		snprintf(s, sizeof (s), "%2d 0x%08X %s %s %10ld %8ld %s\r\n",
		    i, (unsigned int) rtl_pool_svcs[i].svc,
		    rtl_pool_svcs[i].enabled ? "Y" : "N",
		    rtl_pool_svcs[i].run_in_diag ? "Y" : "N",
		    rtl_pool_svcs[i].start_time, rtl_pool_svcs[i].svc_time,
		    rtl_pool_svcs[i].name);

		print_str(s);
	}

	print_str("---------------------------------------------------------\n\r");
	print_str("IRQ# entry[us] min[us] max[us] overrun calls NAME\n\r");
	print_str("---------------------------------------------------------\n\r");

	for (i = 0; i < RTL_ISTAT_NUM; i++) {
		snprintf(s, sizeof (s), "%4d %11ld %10ld %10ld %6ld %6ld %s\n\r",
		    rtl_istat[i].num, rtl_istat[i].prlg_time,
		    rtl_istat[i].min_time, rtl_istat[i].max_time,
		    rtl_istat[i].missed, rtl_istat[i].nloops,
		    rtl_istat[i].name);
		print_str(s);
	}

	print_str("---------------------------------------------------------\n\r");

	/* Print number of used timers. */
	for (i = 0; i < RTL_TIMER_POOL_SIZE && rtl_pool_timers[i].allocated; i++)
		;

	snprintf(s, sizeof (s), "RT timers used: %d/%d\n\r",
	    i, RTL_TIMER_POOL_SIZE);
	print_str(s);
	print_str("---------------------------------------------------------\n\r");

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

/* Runs on CPU1 in GPTimer interrupt context, preserving RTL service order.
 * No unbounded catch-up loop: an overrun is recorded rather than starving IDF.
 * This build only starts heartbeat; application services stay disabled until
 * their entire driver/call graph is verified for ISR use. */
static bool
rtl_on_alarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t *event, void *arg)
{
    (void)timer;
    (void)arg;
    int64_t started = esp_timer_get_time();
    rtl_timer_svc();
    rtl_heartbeat_svc();
    BOOL diag = mon_is_diag_mode();
    for (int i = 0; i < RTL_SVC_POOL_SIZE && rtl_pool_svcs[i].svc; ++i) {
        rtl_svc_t *svc = &rtl_pool_svcs[i];
        if (svc->enabled && (!diag || svc->run_in_diag)) {
            int64_t before = esp_timer_get_time();
            long offset = (long)(before - started);
            svc->svc();
            long duration = (long)(esp_timer_get_time() - before);
            if (offset > svc->start_time) svc->start_time = offset;
            if (duration > svc->svc_time) svc->svc_time = duration;
            diag = mon_is_diag_mode();
        }
    }
    unsigned long duration = (unsigned long)(esp_timer_get_time() - started);
    if (duration < rtl_istat[0].min_time) rtl_istat[0].min_time = duration;
    if (duration > rtl_istat[0].max_time) rtl_istat[0].max_time = duration;
    if (event->count_value > rtl_istat[0].prlg_time) rtl_istat[0].prlg_time = event->count_value;
    if (duration >= RTL_SERVICE_PERIOD) rtl_istat[0].missed++;
    rtl_istat[0].nloops++;
    return false;
}
