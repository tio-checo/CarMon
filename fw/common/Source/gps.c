/*********************************************************************
 *
 * Implements GPS service
 *
 * Communicates with L80 Quectel GPS module via Bluetooth.
 * Receives data in NMEA0183 format and logs them to the log device.
 *
 * Format of NMEA0183 sentence:
 *
 * $<nmea sentence>*<checksum>CRLF
 *
 * where <checksum> is calculated as XOR of ASCII codes
 * of characters enclosed between '$' and '*'.
 *
 *********************************************************************/

#include "bt.h"
#include "carmon.h"
#include "pic_core.h"
#include "rt_light.h"
#include "utils.h"
#include "gps.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

/* local constants */
/* NMEA1083 definitions */
#define	GPS_NMEA_SOF		'$'	/* NMEA 'start of frame' */
#define	GPS_NMEA_CHSUM		'*'	/* NMEA 'start of checksum' */
#define	GPS_NMEA_EOF		'\r'	/* NMEA 'end of frame' */
#define	GPS_NMEA_FIELD_SEP	','	/* NMEA field separator */
#define	GPS_NMEA_GGA		"GPGGA"	/* GGA sentence signature */
#define	GPS_NMEA_GSA		"GPGSA"	/* GSA sentence signature */
#define	GPS_NMEA_RMC		"GPRMC"	/* RMC sentence signature */

/* states of the main state machine */
#define	GPS_ST_WAIT4SOF		0	/* waits for NMEA SOF */
#define	GPS_ST_SENTENCE		1	/* receives NMEA sentence */

/* local variables */
/* State of GPS state machine. */
static int gps_st;

/* Status of GPS device. */
static int gps_status;

/* Course - obtained from RMC sentence. */
static int gps_course;

/* Set to TRUE once HW clock has been synced with GPS. */
static BOOL gps_rtc_updated;

/* Number of checksum errors. */
static unsigned long gps_chsum_err;

/*
 * Buffer holding received NMEA sentence.
 * Per NMEA standard, the sentence should not be longer
 * then 80 characters exluding terminating <CR><LF>.
 * Add a little bit more to be on a safe side.
 */
static char gps_nmea_buf[100];

/* local functions */

/*
 * Returns TRUE if given buffer holds specified NMEA sentence;
 * FALSE otherwise.
 */
static BOOL
gps_is_nmea_sentence(const char *s)
{
	size_t l = strlen(s);

	/* start of frame is not subject of comparison */
	return (strncmp(gps_nmea_buf + 1, s, l) == 0);
}

/*
 * Returns pointer to the specified field within
 * provided NMEA sentence;
 * NULL if the field is not present or is empty.
 */
static char *
gps_get_nmea_field(char *s, int n)
{
	int i;

	/* Seek for n-th field in the sentence. */
	for (i = 0; i < n && s != NULL; i++) {
		char *curr_pos = s;

		s = strchr(curr_pos, GPS_NMEA_FIELD_SEP);

		/* Move to the field content. */
		if (s != NULL) {
			s++;
		}
	}

	/*
	 * Return NULL if given sentence does not contain
	 * n-th field or n-th field is empty.
	 */
	if (s == NULL ||
	    s[0] == GPS_NMEA_FIELD_SEP ||
	    s[0] == GPS_NMEA_CHSUM ||
	    s[0] == '\0') {
		return NULL;
	}

	return s;
}

/*
 * Updates HW clock from GPS data.
 * Done only once after GPS provides valid data.
 */
static void
gps_update_rtc(char *s)
{
	char td[14 + 1];

	if (gps_rtc_updated) {
		return;
	}

	/* time */
	char *t = gps_get_nmea_field(s, 1);
	/* status */
	char *sts= gps_get_nmea_field(s, 2);
	/* date */
	char *d = gps_get_nmea_field(s, 9);

	/* Check if the sentence contains valid data. */
	if (sts == NULL || sts[0] != 'A' ||
	    t == NULL || d == NULL) {
		return;
	}

	/*
	 * Convert GPS data to the format expected by pcore_settime():
	 *             YYYYMMDDHHMMSS
	 */
	/* Year */
	td[0] = '2'; td[1] = '0';
	td[2] = d[4]; td[3] = d[5];

	/* Month */
	td[4] = d[2]; td[5] = d[3];

	/* Day */
	td[6] = d[0]; td[7] = d[1];

	/* Time */
	td[8] = t[0]; td[9] = t[1];
	td[10] = t[2]; td[11] = t[3];
	td[12] = t[4]; td[13] = t[5];

	/* null terminator */
	td[14] = '\0';

	/*
	 * Update HW clock.
	 * Leave SW clock untouched, since at this point
	 * it is already likely used to generate timestamps
	 * for log records.
	 * Drop string NULL-terminator, since this is what
	 * pcore_settime() expects.
	 */
	if (pcore_settime(td, sizeof (td) - 1, TRUE, FALSE) == 0) {
		gps_rtc_updated = TRUE;
	}
}
		
/*
 * GPS receive service routine
 */
static void
gps_rx_svc(unsigned char ch)
{
	/* Index in NMEA sentence buffer */
	static int i;

	BOOL is_gga, is_rmc, is_gsa;
	int chsum;

	switch (gps_st) {
	/* Wait for SOF */
	case GPS_ST_WAIT4SOF:
		if (ch != GPS_NMEA_SOF) {
			break;
		}

		/* SOF received, prepare sentence buffer. */
		gps_nmea_buf[0] = GPS_NMEA_SOF;
		i = 1;

		gps_st = GPS_ST_SENTENCE;

		break;

	/* Receives NMEA sentence. */
	case GPS_ST_SENTENCE:
		/*
		 * '\n' received in the middle of the sentence means
		 * that we have lost connection with the GPS BT module.
		 * Drop the ball in such case and wait for the new
		 * sentence which is to be received once the connection
		 * is established again.
		 */
		if (ch == '\n') {
			gps_st = GPS_ST_WAIT4SOF;

			break;
		}

		/*
		 * SOF received, start from scratch.
		 * Not likely, but also not impossible.
		 */
		if (ch == GPS_NMEA_SOF) {
			gps_nmea_buf[0] = GPS_NMEA_SOF;
			i = 1;

			break;
		}

		/*
		 * Received character is not an EOF mark,
		 * so it is part of a sentence. Store it to the buffer.
		 * If buffer is already full, then discard the character,
		 * but continue to receive the sentence.
		 * Last position in the buffer is reserved for NULL
		 * terminator.
		 */
		if (ch != GPS_NMEA_EOF) {
			if (i >= sizeof(gps_nmea_buf) - 1) {
				break;
			}

			gps_nmea_buf[i++] = ch;
			break;
		}

		/*
		 * Sentence complete.
		 * Verify that at least some data
		 * and check sum have been received.
		 */
		if (i < 4) {
			gps_st = GPS_ST_WAIT4SOF;

			break;
		}

		/*
		 * Calculate and verify check sum.
		 * If check sum is not present, discard the sentence.
		 */
		if (gps_nmea_buf[i - 3] != GPS_NMEA_CHSUM) {
			gps_st = GPS_ST_WAIT4SOF;

			break;
		}

		/* Extract check sum. */
		chsum = hex2ulong(gps_nmea_buf + i - 2, 2);

	 	/* Make sure string is null-terminated. */
		gps_nmea_buf[i] = '\0';

		/*
		 * Calculate the checksum and XOR it with expected value.
		 * Result of such operation should be zero.
		 * Skip start of frame.
		 */
		for (i = 1; gps_nmea_buf[i] != GPS_NMEA_CHSUM; i++) {
			chsum ^= gps_nmea_buf[i];
		}

		if (chsum != 0) {
			/* Increment error counter. */
			gps_chsum_err++;

			gps_st = GPS_ST_WAIT4SOF;

			break;
		}

		/*
		 * Checksum indicator received, sentence is complete.
		 * 'GPGGA', 'GPGSA' and 'GPRMC' sentences are logged,
		 * others are currently discarded.
		 */
		is_gga = gps_is_nmea_sentence(GPS_NMEA_GGA);
		is_gsa = gps_is_nmea_sentence(GPS_NMEA_GSA);
		is_rmc = gps_is_nmea_sentence(GPS_NMEA_RMC);
		if (is_gga || is_gsa || is_rmc) {
			cm_gps_callback(gps_nmea_buf);
		}

		/*
		 * GGA sentence:
		 * $GPGGA,<time>,<lat1>,<lat2>,<long1>,<long2>,<status>,...
		 * Determine status of GPS device.
		 */
		if (is_gga) {
			char *pos = gps_get_nmea_field(gps_nmea_buf, 6);

			/* Field containing status was found. */
			if (pos != NULL) {
				gps_status = pos[0] - '0';
			}
		}

		/*
		 * RMC sentence.
		 * $GPRMC,<HHMMSS>,<sts>,<lat1>,<lat2>,<long1>,<long2>,<speed>,
		 *        <track angle>,<DDMMYY>,<mag var>
		 *  - synchronize HW clock from GPS
		 *  - extract course
		 */
		if (is_rmc) {
			char *course;

			course = gps_get_nmea_field(gps_nmea_buf, 8);
			if (course != NULL) {
				gps_course = dec2ulong(course, 3);
			}

			gps_update_rtc(gps_nmea_buf);
		}

		/* Wait for another NMEA sentence. */
		gps_st = GPS_ST_WAIT4SOF;

		break;
	} /* switch */
}

/*
 * Main service routine.
 *
 * Receives NMEA sentences from GPS and logs them to the log device.
 */
static int
gps_main_svc(void)
{
	unsigned char ch;

	/*
	 * If there is a character from GPS module available,
	 * let receive service routine process it.
	 * Dry out HW receive buffer.
	 */
	while (bt_getchar_gps(&ch) == 0) {
		gps_rx_svc(ch);
	}

	return 0;
}

/* global functions */

/* Main initialization routine. */
int
gps_init(void)
{
	/* Initialize GPS state machine. */
	gps_st = GPS_ST_WAIT4SOF;

	/* GPS not synced yet. */
	gps_status = 0;

	/* HW clock not synced yet. */
	gps_rtc_updated = FALSE;

	/* Clear check sum error counter. */
	gps_chsum_err = 0;

	/* Register and enable main service routine. */
	rtl_svc_init("GPS", gps_main_svc, TRUE, TRUE);

	return 0;
}

/* Returns TRUE if GPS device is synced; FALSE otherwise. */
int
gps_get_status(void)
{
	return gps_status;
}

/* Returns current course - obtained from RMC sentence. */
char
gps_get_course(void)
{
	static const char course_str[] =
	   {'S', 's', 'V', 'v', 'J', 'j', 'Z', 'z', 'S'};

	int i = (gps_course * 2 + 45) / 90;

	if (i < 0 || i > 8) {
		return 'u';
	}

	return course_str[i];
}

/*
 * Prints diagnostic information. 
 */
void
gps_print_diag_info(int (*print_str)(const char *))
{
	char s[81];

	/* header */
	print_str("-------------------------------------\r\n");

	/* BT status */
	snprintf(s, sizeof(s), "BT paired: %s\r\n",
	    bt_is_paired() ? "Yes" : "No");
	print_str(s);
	snprintf(s, sizeof(s), "Paired with GPS: %s\r\n",
	    bt_is_paired_with_gps() ? "Yes" : "No");
	print_str(s);

	/* Number of checksum errors. */
	snprintf(s, sizeof(s), "Chsum errors: %lu\r\n", gps_chsum_err);
	print_str(s);
	
	/* GPS status */
	snprintf(s, sizeof(s), "GGA fix quality: %d\r\n", gps_status);
	print_str(s);

	/* HW clock synced with GPS? */
	snprintf(s, sizeof(s), "HW clock synced: %s\r\n",
	    gps_rtc_updated ? "Yes" : "No");
	print_str(s);

	/* Last received sentence - might be incomplete. */
	snprintf(s, sizeof(s), "s: %s\r\n", gps_nmea_buf);
	print_str(s);

	/* footer */
	print_str("-------------------------------------\n\r");	
}
