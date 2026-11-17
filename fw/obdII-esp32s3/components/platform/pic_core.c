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
#include "dma.h"

#include "master_comm.h"

#ifdef MCP9801_MODULE
#include "mcp9801.h"
#endif

#ifdef PCF8563_MODULE
#include "pcf8563.h"
#endif

#ifdef I2CHAL_MODULE
#include "i2c_hal.h"
#endif

#ifdef EE24_MODULE
#include "ee24.h"
#endif

#include "helpers.h"
#include "pic_core.h"
#include "rt_light.h"
#include "strings.h"
#include "utils.h"

/* constants */
#ifdef I2CHAL_MODULE
/* I2C data buffer size */
#define PCORE_I2C_BUF_SIZE	256
#endif

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

#ifdef I2CHAL_MODULE
/* I2C message */
static i2c_msg_t i2c_msg;
/* I2C data buffer */
static unsigned char i2c_buf[PCORE_I2C_BUF_SIZE];
#endif

/* state machine handling I2C communication */
static enum {
	PCORE_ST_IDLE = 0,	/* state machine idle */

	PCORE_EE_ST_WAIT4SETPAR,	/* send ACK after EEPROM written */

#ifdef I2CHAL_MODULE
	PCORE_I2C_ST_READ1,	/* Read from device */
	PCORE_I2C_ST_READ2,

	PCORE_I2C_ST_WRITE1,		/* Write to device */
	PCORE_I2C_ST_WRITE2
#endif
} pcore_st;

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
	fl_send_ack_nak = FALSE;

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
#endif

	/* Update time of the day. */
	updatetimeofday(RTL_SERVICE_PERIOD);

	/* Handle communication service. */
	pcore_comm_svc();

#ifdef PCF8563_MODULE
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

	switch (pcore_st) {
	/* Idle */
	case PCORE_ST_IDLE:
		break;

	/*
	 * Wait for EEPROM parameter stored in EEPROM,
	 * send ACK to master afterwards.
	 */
	case PCORE_EE_ST_WAIT4SETPAR:
		if (ee24_svc_busy()) {
			break;
		}

		pcore_st = PCORE_ST_IDLE;
		mcom_prepare_ack_response(&pcore_resp_ack_nak);
		fl_send_ack_nak = TRUE;
		break;

#ifdef I2CHAL_MODULE
	/* Read from I2C device - grab the bus*/
	case PCORE_I2C_ST_READ1:
		if (i2c_request_bus() == 0) {
			pcore_st = PCORE_I2C_ST_READ2;
		}

		break;

	/* Read from I2C device */
	case PCORE_I2C_ST_READ2:
		if (i2c_random_read(i2c_msg.dev_addr, i2c_msg.data_addr,
		    i2c_msg.buf, i2c_msg.ln) == EAGAIN) {
			break;
		}

		/* Release the bus */
		i2c_release_bus();

		/* Send data to master */
		pcore_send_data_response_to_master(i2c_msg.buf,
		    i2c_msg.ln, MCOM_DATA_TYPE_BYTE);

		pcore_st = PCORE_ST_IDLE;
		break;

	/* Write to I2C device - grab the bus */
	case PCORE_I2C_ST_WRITE1:
		if (i2c_request_bus() == 0) {
			pcore_st = PCORE_I2C_ST_WRITE2;
		}

		break;

	/* Write to I2C device */
	case PCORE_I2C_ST_WRITE2:
		if (i2c_write(i2c_msg.dev_addr, i2c_msg.buf, i2c_msg.ln)
		    == EAGAIN) {
			break;
		}

		/* Release the bus */
		i2c_release_bus();

		/* Send result of the operation to master */
		mcom_prepare_ack_response(&pcore_resp_ack_nak);
		fl_send_ack_nak = TRUE;

		pcore_st = PCORE_ST_IDLE;
		break;
	}
#endif

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

	/* initialize pcore state machine */
	pcore_st = PCORE_ST_IDLE;
#ifdef I2CHAL_MODULE
	/* initialize pointer to I2C data buffer */
	i2c_msg.buf = i2c_buf;
#endif

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
	static unsigned char resp_data[6];

	int err = 0;
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
			} else {
				mcom_chsum_configure(hex2ulong(data + 1, 2));
			}
			break;

		/* Set time and date */
		case MCOMM_PCORE_CMD_SETTIME:
			/* Update both sw and hw clock. */
			err = pcore_settime(data + 1, ln - 1, TRUE, TRUE);
			/* The only error returned is EINVAL for now. */
			if (err != 0) {
				err = MCOM_NAK_EINVAL;
			}
			break;

		/* Get time and date */
		case MCOMM_PCORE_CMD_GETTIME:
			tds = pcore_gettime();

			pcore_send_data_response_to_master(tds, strlen(tds),
			    MCOM_DATA_TYPE_ASCII);

			break;

#ifdef MCP9801_MODULE
		/* Get temperature */
		case MCOMM_PCORE_CMD_GETTEMP: {
			unsigned short temp_us = mcp9801_get_temperature();

			resp_data[0] = (unsigned char)(temp_us >> 8);
			resp_data[1] = (unsigned char)temp_us;

			pcore_send_data_response_to_master(resp_data, 2,
			    MCOM_DATA_TYPE_BYTE);

		} break;
#endif

#ifdef EE24_MODULE
		/* Get EEPROM parameter */
		case MCOMM_PCORE_CMD_GETEEPAR: {
			int size;
			unsigned char *pvalue;

			err = ee24_getpar(hex2ulong(data + 1, 2), &size,
			    &pvalue);
			/* The only error returned is EINVAL. */
			if (err != 0) {
				err = MCOM_NAK_EINVAL;
			} else {
				pcore_send_data_response_to_master(pvalue, size,
				    MCOM_DATA_TYPE_BYTE);
			}
		} break;

		/* Set EEPROM parameter */
		case MCOMM_PCORE_CMD_SETEEPAR: {
			/*
			 * At least parameter number and one byte of value has
			 * to be provided.
			 */
			if (ln < 5 || (ln % 2 == 0)) {
				err = MCOM_NAK_EINVAL;
			} else {
				err = ee24_setpar(hex2ulong(data + 1, 2),
				    ln - 3, data + 3);

				/* The only error returned is EINVAL. */
				if (err != 0) {
					err = MCOM_NAK_EINVAL;
				} else {
					/*
					 * Wait until parameter is stored
					 * to EEPROM, then send ACK.
					 */
					pcore_st = PCORE_EE_ST_WAIT4SETPAR;
					fl_send_ack_nak = FALSE;
				}
			}
		} break;
#endif

#ifdef I2CHAL_MODULE
		/* Read from I2C device */
		case MCOMM_PCORE_CMD_READI2C:
			/* Previous I2C operation still in progress. */
			if (pcore_st != PCORE_ST_IDLE) {
				err = MCOM_NAK_MODULE_BUSY;
				break;
			}

			/* Verify packet length */
			if (ln != 7) {
				err = MCOM_NAK_EINVAL;
				break;
			} 

			i2c_msg.dev_addr = hex2ulong(data + 1, 2);
			i2c_msg.data_addr = hex2ulong(data + 3, 2);
			i2c_msg.ln = hex2ulong(data + 5, 2);

			/* Make sure the data fit the buffer */
			if (i2c_msg.ln > sizeof (i2c_buf)) {
				err = MCOM_NAK_EINVAL;
				break;
			} 

			pcore_st = PCORE_I2C_ST_READ1;

			fl_send_ack_nak = FALSE;
			break;

		/* Write to I2C device */
		case MCOMM_PCORE_CMD_WRITEI2C: {
			int i;

			/* Previous I2C operation still in progress. */
			if (pcore_st != PCORE_ST_IDLE) {
				err = MCOM_NAK_MODULE_BUSY;
				break;
			}

			/* Verify packet length */
			if ((ln < 7) || ((ln - 3) / 2 > sizeof (i2c_buf))) {
				err = MCOM_NAK_EINVAL;
				break;
			}

			i2c_msg.dev_addr = hex2ulong(data + 1, 2);

			/*
			 * Transfer data to I2C buffer starting
			 * with address to be written.
			 */
			for (i = 0; i < (ln - 3) / 2; i++) {
				i2c_msg.buf[i] = hex2ulong(data + 3 + i * 2, 2);
			}

			i2c_msg.ln = (ln - 3) / 2;

			pcore_st = PCORE_I2C_ST_WRITE1;

			fl_send_ack_nak = FALSE;

		} break;
#endif

		/*
		 * Calculate CRC checksum for given data.
		 * 'data' buffer is in form of
		 *
		 * ------------------------------
		 * | c | TYPE | P1..P4 | D1..DN |
		 * ------------------------------
		 *
		 * TYPE (1 char)
		 *  s - 16bit CRC-CCITT calculated by software (only P4 used)
		 *  d - CRC calculated by DMA module
		 * P1 (1b)
		 *  ==0 - input data not reflected (MSB first)
		 *  !=0 - input data reflected (LSB first)
		 * P2 (1b)
		 *  length of CRC polynomial (max 32)
		 * P3 (4b)
		 *  CRC polynomial
		 * P4 (4b)
		 *  CRC seed
		 * D1..DN
		 *  data
		 */
		case MCOMM_PCORE_CMD_CALCCRC: {
			unsigned int crc = 0;
			unsigned int t;
			unsigned int crc_polynomial;
			unsigned int crc_seed;
			unsigned int crc_plen;
			unsigned int crc_refin;
			unsigned char *crc_dbuf;
			unsigned int crc_dln;

			/*
			 * Do some sanity checking.
			 * At least one data byte is required.
			 */
			if ((data[1] != 's' && data[1] != 'd')
			    || ln < 24 || (ln % 2 != 0)) {
				err = MCOM_NAK_EINVAL;
				break;
			}

			/* Convert CRC parameters. */
			crc_refin = hex2ulong(data + 2, 2);
			crc_plen = hex2ulong(data + 4, 2);
			crc_polynomial = hex2ulong(data + 6, 8);
			crc_seed = hex2ulong(data + 14, 8);

			if (crc_plen < 1 || crc_plen > 32) {
				err = MCOM_NAK_EINVAL;
				break;
			}

			/* Convert input ASCII HEX data to sequence of bytes. */
			crc_dbuf = (unsigned char *)data + 22;
			crc_dln = (ln - 22) / 2;
			if (hex2ucharbuf(data + 22, crc_dbuf, ln - 22) != 0) {
				err = MCOM_NAK_EINVAL;
				break;
			}

			if (data[1] == 's') {
				t = TMR1;
				crc = crc_calc_array(crc_dbuf, crc_dln,
				    crc_seed);
				t = TMR1 - t;
			} else {
				t = TMR1;
				/*
				 * Configure DMA module:
				 *  - use DMA channel 0 with priority 0
				 *  - CRC checksum is to be calculated
				 *  - input data reflected or not
				 *  - bytes not reordered
				 */
				/* Sets DRCCON */
				DmaSfmConfigure(DMA_CHKSUM_CRC, crc_refin == 0 ?
				    DMA_BITO_MSb : DMA_BITO_LSb,
				    DMA_REORDER_NOT);
				/* Sets DRCCON, DCRCDATA, DCRCXOR */
				DmaSfmCrcConfigure(crc_polynomial, crc_plen,
				    crc_seed);
				/* Blocks until DMA is done with CRC calc. */
				if (DmaChnMemCrc(&crc, crc_dbuf, crc_dln,
				    DMA_CHANNEL0, DMA_CHN_PRI0) !=
				    DMA_TXFER_OK) {
					err = MCOM_NAK_IO_ERROR;
					break;
				}
				t = TMR1 - t;
			}

			t /= GetPeripheralClock() / 1000000;

			resp_data[0] = (unsigned char)(crc >> 24);
			resp_data[1] = (unsigned char)(crc >> 16);
			resp_data[2] = (unsigned char)(crc >> 8);
			resp_data[3] = (unsigned char)crc;
			resp_data[4] = (unsigned char)(t >> 8);
			resp_data[5] = (unsigned char)t;

			pcore_send_data_response_to_master(resp_data, 6,
			    MCOM_DATA_TYPE_BYTE);

		} break;

		/* unknown command */
		default:
			err = MCOM_NAK_UNKNOWN_IDCMD;

			break;
	}

	if (fl_send_ack_nak) {
		/*
		 * Send response to master indicating
		 * that command was processed.
		 */
		if (err == 0) {
			mcom_prepare_ack_response(&pcore_resp_ack_nak);
		} else {
			mcom_prepare_nak_response(&pcore_resp_ack_nak, err);
		}
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
#ifdef EE24_MODULE
	/* Both time as well as EEPROM parameters must be initialized. */
	return (pcore_time_initialized && ee24_initialized());
#else
	/* Time service must be initialized. */
	return (pcore_time_initialized);
#endif
}
