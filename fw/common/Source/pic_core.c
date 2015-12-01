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
/* real time initialized */
static BOOL pcore_time_initialized = FALSE;

/* local functions */
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
	    buf, type, ln, MCOM_DONT_FRAGMENT);

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
 * Sets date and time. Validates provided data.
 * Returns EINVAL value if provided configuration is invalid.
 */
int
pcore_settime(char *td, int ln, BOOL update_hw_clock, BOOL update_sw_clock)
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

	if (update_sw_clock) {
		if (settimeofday(&newtv, NULL) != 0) {
			return EINVAL;
		}
	}

#ifdef PCF8563_MODULE
	/* Set time in RTC */
	if (update_hw_clock) {
		pcf8563_settime(&newtm);
	}
#endif

	return 0;
}

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
	static unsigned short temp_us;

	int err, i;
	BOOL respond_with_ack = TRUE;
	char *tds;

	/* Assume ACK/NAK will be sent to master */
	fl_send_ack_nak = TRUE;

	/* Handle particular commands */
	switch (data[0]) {
		/* Configure communication parameters */
		case MCOMM_PCORE_CMD_COMCFG:
			/* Verify packet length */
			if (ln != 3) {
				err = MCOM_NAK_EINVAL;
				respond_with_ack = FALSE;
			} else {
				mcom_chsum_configure(hex2ulong(data + 1, 2));
			}
			break;

		/* Set time and date */
		case MCOMM_PCORE_CMD_SETTIME:
			/* Update both sw and hw clock. */
			err = pcore_settime(data + 1, ln - 1, TRUE, TRUE);
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
		case MCOMM_PCORE_CMD_GETTEMP:
			temp_us = mcp9801_get_temperature();

			pcore_send_data_response_to_master(&temp_us, 1,
			    MCOM_DATA_TYPE_WORD);

			/* Data will be sent to master instead of ACK/NAK */
			fl_send_ack_nak = FALSE;
		break;
#endif

		/* Calculate 16bit CRC-CCITT checksum for given data */
		case MCOMM_PCORE_CMD_CALCCRC:
			crc_init();

			for (i = 1; i < ln; i++) {
				crc_calc(data[i]);
			}

			temp_us = crc_finalize();

			pcore_send_data_response_to_master(&temp_us, 1,
			    MCOM_DATA_TYPE_WORD);

			/* Data will be sent to master instead of ACK/NAK */
			fl_send_ack_nak = FALSE;
		break;

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
	unsigned long t1, t2;

	t1 = TMR1;

	get_curr_hrtime(&currtv);
	t = currtv.tv_sec;
	currtm = gmtime(&t);

	/* Display leading zeros. */
	long2str(currtm->tm_year + 1900, tds, 4, TRUE);
	long2str(currtm->tm_mon + 1, tds + 4, 2, TRUE);
	long2str(currtm->tm_mday, tds + 6, 2, TRUE);
	long2str(currtm->tm_hour, tds + 8, 2, TRUE);
	long2str(currtm->tm_min, tds + 10, 2, TRUE);
	long2str(currtm->tm_sec, tds + 12, 2, TRUE);
	long2str(currtv.tv_usec / 1000, tds + 14, 3, TRUE);

	tds[17] = '\0';

	t2 = TMR1;

	if (t2 > t1) {
		t2 -= t1;

		if (pcore_gettime_time < t2) {
			pcore_gettime_time = t2;
		}
	}

	return tds;
}

/*
 * Returns TRUE if core services are up and running; FALSE otherwise.
 */
BOOL
pcore_svc_online(void)
{
	/* Only time service checked at this point. */
	return pcore_time_initialized;
}
