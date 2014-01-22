/*********************************************************************
 *
 * Support for PIC core functions.
 *
 *********************************************************************/

#include <errno.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "HardwareProfile.h"
#include "xc.h"

#include "master_comm.h"

#ifdef MCP9801_MODULE
#include "mcp9801.h"
#endif

#ifdef PCF8563_MODULE
#include "pcf8563.h"
#endif

#include "helpers.h"
#include "pic_core.h"
#include "rt_light.h"
#include "strings.h"
#include "utils.h"

/* constants */

/* local variables */
/* communication module */
/* structure holding ACK/NAK response to master */
static mcom_response_t pcore_resp_ack_nak;
/* If set to TRUE, send ack/nak response packet to master */
static BOOL fl_send_ack_nak;

/* structure holding data response to master */
static mcom_response_t pcore_resp_data;
/* If set to TRUE, send response containing data to master */
static BOOL fl_send_data;

/* local functions */
/*
 * Set date and time. Validates provided data.
 * Returns EINVAL value if provided configuration is invalid.
 */
static int
pcore_settime(char *td, int ln)
{
	time_t t;
	struct tm newtm;
	struct timeval newtv;

	/* malformed packet */
	if (ln != 14)
		return EINVAL;

	/* td[0..3] - year */
	newtm.tm_year = (int) dec2ulong(td, 4);
	if (newtm.tm_year < 1970 || newtm.tm_year > 2038)
		return EINVAL;
	newtm.tm_year -= 1900;

	/* td[4..5] - month */
	newtm.tm_mon = (int) dec2ulong(td + 4, 2) - 1;
	if (newtm.tm_mon < 0 || newtm.tm_mon > 11)
		return EINVAL;

	/* td[6..7] - day */
	newtm.tm_mday = (int) dec2ulong(td + 6, 2);
	if (newtm.tm_mday < 1 || newtm.tm_mday > 31)
		return EINVAL;

	/* td[8..9] - hour */
	newtm.tm_hour = (int) dec2ulong(td + 8, 2);
	if (newtm.tm_hour < 0 || newtm.tm_hour > 23)
		return EINVAL;

	/* td[10..11] - minute */
	newtm.tm_min = (int) dec2ulong(td + 10, 2);
	if (newtm.tm_min < 0 || newtm.tm_min > 59)
		return EINVAL;

	/* td[12..13] - seconds 00..59 */
	newtm.tm_sec = (int) dec2ulong(td + 12, 2);
	if (newtm.tm_sec < 0 || newtm.tm_sec > 59)
		return EINVAL;

	t = mktime(&newtm);
	if (t == (time_t) -1)
		return EINVAL;

	newtv.tv_sec = t;
	newtv.tv_usec = 0;

	if (settimeofday(&newtv, NULL) != 0)
		return EINVAL;

#ifdef PCF8563_MODULE
	/* Set time in RTC */
	pcf8563_settime(&newtm);
#endif

	return 0;
}

/*
 * Takes care of sending response to master. Two responses might be scheduled
 * for transmission - ack/nak one and data one.
 * ack/nak response takes precedence over data response, since it is short,
 * thus it will occupy transmitter for shorter time.
 */
static int
pcore_comm_svc(void)
{
	/*
	 * There is ack/nak response to be sent to master,
	 * queue it for transmission.
	 */
	if (fl_send_ack_nak) {
		mcom_queue_response(&pcore_resp_ack_nak);

		fl_send_ack_nak = FALSE;
	}

	/*
	 * There is data response to be sent to master,
	 * queue it for transmission.
	 */
	if (fl_send_data) {
		mcom_queue_response(&pcore_resp_data);

		fl_send_data = FALSE;
	}

	return 0;
}

/*
 * Sends data packet to master.
 */
static int
pcore_send_data_response_to_master(void *buf, int ln, int type)
{
	mcom_prepare_generic_response(&pcore_resp_data, "00",
	    buf, type, ln);

	fl_send_data = TRUE;

	return 0;
}

/*
 * Main acquisition service routine - called periodically from timer interrupt.
 */
static int
pcore_svc(void)
{
#ifdef PCF8563_MODULE
	static BOOL pcore_time_initialized = FALSE;

	time_t t;
	struct tm newtm;
	struct timeval newtv;

	/* initialize time from RTC */
	if (!pcore_time_initialized) {
		switch (pcf8563_gettime(&newtm)) {
		/* operation in progress */
		case EAGAIN:
			break;

		/* operation successfully finished */
		case 0:
			t = mktime(&newtm);
			if (t != (time_t) -1) {
				newtv.tv_sec = t;
				newtv.tv_usec = 0;

				settimeofday(&newtv, NULL);
			}

			/* PASS THROUGH */

		/* operation failed - do not initialize core time from RTC */
		default:
			pcore_time_initialized = TRUE;
			break;
		} /*switch*/
	}
#endif

	/* Update time of the day. */
	updatetimeofday(RTL_SERVICE_PERIOD);

	/* Handle communication service. */
	pcore_comm_svc();

	return 0;
}

/* global functions */
/*
 * Initialization routine.
 */
int
pcore_init(void)
{
	/* initialize communication data */
	pcore_resp_ack_nak.id = MCOMM_PIC_ID;
	pcore_resp_data.id = MCOMM_PIC_ID;

	/* Register and enable pcore service */
	rtl_svc_init("PicCore", pcore_svc, TRUE, TRUE);

	return 0;
}

/*
 * Processes query from master targeted to PIC core module
 */
int
pcore_process_query(char *data, int ln)
{
	int err;
	BOOL respond_with_ack = TRUE;
	char *tds;

	/* Assume ACK/NAK will be sent to master */
	fl_send_ack_nak = TRUE;

	/* Handle particular commands */
	switch (data[0]) {
		/* Set time and date */
		case MCOMM_PCORE_CMD_SETTIME:
			/* configure module */
			err = pcore_settime(data + 1, ln - 1);
			if (err != 0)
				respond_with_ack = FALSE;
			break;

		/* Get time and date */
		case MCOMM_PCORE_CMD_GETTIME:
			tds = pcore_gettime();

			pcore_send_data_response_to_master(tds, strlen(tds),
			    MCOM_DATA_TYPE_ASCII);

			/* Data will be sent to master instead of ACK/NAK */
			fl_send_ack_nak = FALSE;
			break;

#ifdef MCP9801_MODULE
		/* Get temperature */
		case MCOMM_PCORE_CMD_GETTEMP: {
			static unsigned int temp;

			temp = mcp9801_get_temperature();

			pcore_send_data_response_to_master(&temp, 1,
			    MCOM_DATA_TYPE_WORD);

			/* Data will be sent to master instead of ACK/NAK */
			fl_send_ack_nak = FALSE;
		} break;
#endif

		/* unknown command */
		default:
			err = MCOM_NAK_UNKNOWN_IDCMD;
			respond_with_ack = FALSE;

			break;
	}

	/*
	 * Send response to master indicating that command was processed.
	 */
	if (respond_with_ack) {
		mcom_prepare_ack_response(&pcore_resp_ack_nak);
	} else {
		mcom_prepare_nak_response(&pcore_resp_ack_nak, err);
	}

	return 0;
}

/*
 * Reads date and time.
 * Returns pointer to resulting string.
 */
char *
pcore_gettime(void)
{
	static char tds[18];

	time_t t;
	struct tm *currtm;
	struct timeval currtv;

	get_curr_hrtime(&currtv);
	t = currtv.tv_sec;
	currtm = gmtime(&t);

	snprintf(tds, sizeof (tds), "%04d%02d%02d%02d%02d%02d%03ld",
	    currtm->tm_year + 1900, currtm->tm_mon + 1, currtm->tm_mday,
	    currtm->tm_hour, currtm->tm_min, currtm->tm_sec,
	    currtv.tv_usec / 1000);

	return tds;
}

