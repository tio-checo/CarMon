/*********************************************************************
 *
 * Support for RFID ID20 reader. Waits for messages from the reader
 * and passes them to the master via Bluetooth interface.
 *
 * ID20 message structure - ASCII format
 * -------------------------------------------------------------------------
 * | STX (02h) | DATA (10 HEX ASCII) | CHSUM (2 HEX ASCII) | CR | LF | ETX |
 * -------------------------------------------------------------------------
 *
 *********************************************************************/

#include <ctype.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>

#include "HardwareProfile.h"
#include "xc.h"
#include "uart.h"

#include "log.h"
#include "master_comm.h"
#include "pic_core.h"
#include "rfid.h"
#include "rt_light.h"
#include "strings.h"
#include "utils.h"

/* constants */
/* communication pins */
#define RFID_RX_PIN	2	/* RD2 */
#define RFID_RX_MASK	(1 << RFID_RX_PIN)
#define RFID_TX_PIN	3	/* RD3 */
#define RFID_TX_MASK	(1 << RFID_TX_PIN)

/* communication speed */
#define RFID_BAUDRATE	9600

/* Start of the RFID packet. */
#define RFID_PKT_STX	0x02
/* End of the RFID packet. */
#define RFID_PKT_ETX	0x03

/* Length of data portion of RFID message including check sum. */
#define RFID_MSG_LN		12

/* Length of timestamp - YYYYMMDDHHMMSSmmm */
#define RFID_TIMESTAMP_LN	17

/*
 * Size of buffer for RFID message to be sent to master. Account for
 * null termination.
 */
#define RFID_MSG_BUF_LN	RFID_TIMESTAMP_LN + RFID_MSG_LN + 1

/*
 * Size of log record containing RFID message - log record is terminated
 * with \r\n.
 */
#define RFID_LOG_RECORD_SIZE	RFID_MSG_LN + RFID_TIMESTAMP_LN + 2

/* local types */
/* character received from ID20 */
typedef struct rfid_rx_char {
	unsigned char	ch;
	unsigned long	sts;
} rfid_rx_char_t;

/* local variables */
/* state machine handling communication with ID20 RFID reader */
static enum {
	RFID_ID20_ST_IDLE = 0,	/* state machine idle */
	RFID_ID20_ST_WAIT4STX,	/* waits for start of frame */
	RFID_ID20_ST_RXDATA,	/* receives data */
	RFID_ID20_ST_WAIT4CR,	/* waits for CR */
	RFID_ID20_ST_WAIT4LF,	/* waits for LF */
	RFID_ID20_ST_WAIT4ETX	/* waits for ETX */
} rfid_id20_st;

/* state machine handling communication with logging service */
static enum {
	RFID_LOG_ST_IDLE = 0,	/* state machine idle */
	RFID_LOG_ST_GET_FIRST,	/* get the first logged record */
	RFID_LOG_ST_GET_NEXT,	/* get next logged record */
	RFID_LOG_ST_WAIT4CMD	/* waits for logging command complete */
} rfid_log_st;

/*
 * When set to TRUE, messages received from RFID reader are immediately
 * sent to master via Bluetooth interface.
 */
static BOOL rfid_send2bt;

/* Buffer handling record received from RFID reader. */
static char rfid_msg_buf[RFID_MSG_BUF_LN];

/* communication module */
/* structure holding ACK/NAK response to master */
static mcom_response_t rfid_resp_ack_nak;
/* If set to TRUE, send ack/nak response packet to master */
static BOOL fl_send_ack_nak;

/* structure holding data response to master */
static mcom_response_t rfid_resp_data;
/* If set to TRUE, send response containing data to master */
static BOOL fl_send_data;

/* heartbeat LED blinking mode */
static unsigned char rfid_heartbeat_mode;

/* When set to TRUE, turn the LED on for a while. */
static BOOL rfid_turn_led_on;

/* Packet sequence. Used by master to detect packet loss. */
static int pkt_num;

/* Packet header - null-terminated string (two ASCII HEX characters). */
static char pkt_hdr[3];

/* local functions */
/*
 * handle heartbeat LED
 */
static void
rfid_heartbeat_svc(void)
{
	static int hb_counter = RTL_TIME_250_MSEC;
	static int hb_num_ticks = 1;
	static int hb_curr_tick = 1;

	/* Indicate packet received from RFID reader. */
	if (rfid_turn_led_on) {
		rfid_turn_led_on = FALSE;

		/* Turn LED on for 1 second. */
		LATBCLR = 1 << 6;
		hb_counter = RTL_TIME_1_SEC;

		rfid_heartbeat_mode = HB_MODE_ON;
	} else {
		rfid_heartbeat_mode = HB_MODE_TICKS;

		hb_num_ticks = 1;
	}

	switch (rfid_heartbeat_mode) {
	case HB_MODE_FIFTY_FIFTY:
		if (--hb_counter <= 0) {
			hb_counter = RTL_TIME_250_MSEC;
			LATBINV = 1 << 6;
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
				LATBSET = 1 << 6;
			} else {
				LATBCLR = 1 << 6;
			}
		} else {
			/* silent period */
			if (--hb_counter <= 0) {
				hb_counter = RTL_TIME_200_MSEC;
				hb_curr_tick = hb_num_ticks;
				LATBCLR = 1 << 6;
			}
		}
		break;

	case HB_MODE_ON:
		if (--hb_counter <= 0) {
			/* Turn the LED off */
			LATBSET = 1 << 6;

			/*
			 * Switch to the normal mode - start with period
			 * of silence.
			 */
			hb_curr_tick = 0;
			hb_counter = RTL_TIME_1_SEC;
			rfid_heartbeat_mode = HB_MODE_TICKS;
		}
		break;

	}
}

/*
 * Receive character from ID20. Save receive errors if there are any.
 * Return EAGAIN if buffer is empty.
 */
static int
rfid_getchar(rfid_rx_char_t *rfid_ch)
{
	/* Receive buffer is empty */
	if (!U1STAbits.URXDA) {
		/* If receiving was disabled due to 'buffer overflow',
		 * re-enable it by clearing OERR error bit.
		 */
		if (U1STAbits.OERR)
			U1STAbits.OERR = 0;

		return EAGAIN;
	}

	/*
	 * There is a character in receive buffer, return it along
	 * with related status error bits. Need to get error bits first -
	 * they are lost once character is pulled from buffer.
	 */
	rfid_ch->sts = U1STA & 0x000E;
	rfid_ch->ch = U1RXREG;

	return 0;
}

/*
 * Takes care of sending response to master. Two responses might be scheduled
 * for transmission - ack/nak one and data one.
 * ack/nak response takes precedence over data response, since it is short,
 * thus it will occupy transmitter for shorter time.
 */
static int
rfid_comm_svc(void)
{
	/*
	 * There is ack/nak response to be sent to master,
	 * queue it for transmission.
	 */
	if (fl_send_ack_nak) {
		mcom_queue_response(&rfid_resp_ack_nak);

		fl_send_ack_nak = FALSE;
	}

	/*
	 * There is data response to be sent to master,
	 * queue it for transmission.
	 */
	if (fl_send_data) {
		mcom_queue_response(&rfid_resp_data);

		fl_send_data = FALSE;
	}

	return 0;
}

/*
 * Sends data packet to master.
 */
static int
rfid_send_data_response_to_master(char *pkt_hdr, char *buf, int ln)
{
	mcom_prepare_generic_response(&rfid_resp_data, pkt_hdr,
	    buf, MCOM_DATA_TYPE_ASCII, ln);

	fl_send_data = TRUE;

	return 0;
}

/*
 * Handles communication with ID20 RFID reader.
 */
static int
rfid_id20_svc(void)
{
	static int index;

	rfid_rx_char_t ch;

	/*
	 * If machine is idle or there is no character from ID20 received
	 * or malformed character has been received, return, as there is
	 * nothing to do.
	 */
	if ((rfid_id20_st == RFID_ID20_ST_IDLE) ||
	    (rfid_getchar(&ch) == EAGAIN) || (ch.sts != 0))
		return 0;

	switch (rfid_id20_st) {
	/* service idle */
	case RFID_ID20_ST_IDLE:
		break;

	/* Wait for STX. */
	case RFID_ID20_ST_WAIT4STX:
		if (ch.ch != RFID_PKT_STX)
			break;

		/* Initialize message buffer. */
		index = 0;

		/* Start receiving data */
		rfid_id20_st = RFID_ID20_ST_RXDATA;

		break;

	/* Receive data. */
	case RFID_ID20_ST_RXDATA:
		/* STX received - restart receive process. */
		if (ch.ch == RFID_PKT_STX) {
			index = 0;
			break;
		}

		/*
		 * Check that valid data character has been received.
		 * Only ASCII HEX characters are allowed.
		 * If invalid character has been received, restart
		 * receive process.
		 */
		if (isxdigit(ch.ch) == 0) {
			rfid_id20_st = RFID_ID20_ST_WAIT4STX;
			break;
		}

		/*
		 * Store received character to the buffer. Leave space
		 * for a timestamp which will be added later right before
		 * message is sent to master.
		 */
		rfid_msg_buf[RFID_TIMESTAMP_LN + index++] = ch.ch;

		/* Data portion complete, receive end of the packet. */
		if (index == RFID_MSG_LN) {
			rfid_id20_st = RFID_ID20_ST_WAIT4CR;
		}

		break;

	/* waits for CR */
	case RFID_ID20_ST_WAIT4CR:
		/* If CR not received, restart the whole receive process. */
		if (ch.ch != '\r') {
			rfid_id20_st = RFID_ID20_ST_WAIT4STX;
		} else {
			rfid_id20_st = RFID_ID20_ST_WAIT4LF;
		}

		break;

	/* waits for LF */
	case RFID_ID20_ST_WAIT4LF:
		/* If LF not received, restart the whole receive process. */
		if (ch.ch != '\n') {
			rfid_id20_st = RFID_ID20_ST_WAIT4STX;
		} else {
			rfid_id20_st = RFID_ID20_ST_WAIT4ETX;
		}

		break;

	/* waits for ETX */
	case RFID_ID20_ST_WAIT4ETX:
		/* If LF not received, restart the whole receive process. */
		if (ch.ch != RFID_PKT_ETX) {
			rfid_id20_st = RFID_ID20_ST_WAIT4STX;
			break;
		}

		/*
		 * Valid packet received. Turn on heartbeat LED for a while
		 * and pass the packet to master if applicable.
		 */
		rfid_turn_led_on = TRUE;

		if (rfid_send2bt) {
			char *ts;

			/* Prepend message with timestamp. */
			ts = pcore_gettime();
			strncpy(rfid_msg_buf, ts, RFID_TIMESTAMP_LN);

			/* Convert sequence number to ASCII HEX string. */
			snprintf(pkt_hdr, sizeof (pkt_hdr), "%02X", pkt_num);

			/* Increment sequence number. */
			if (++pkt_num > 255)
				pkt_num = 1;
			
			/* Send the message to the master. */
			rfid_send_data_response_to_master(pkt_hdr, rfid_msg_buf,
			    RFID_TIMESTAMP_LN + RFID_MSG_LN);
		}

		/* Log the record to the permanent storage. */
		log_post(rfid_msg_buf);

		rfid_id20_st = RFID_ID20_ST_WAIT4STX;

		break;

	} /* switch */

	return 0;
}

/*
 * Handles communication with logging service.
 */
static int
rfid_log_svc(void)
{
	int err = 0;
	char *rec;

	switch (rfid_log_st) {
	/* state machine idle */
	case RFID_LOG_ST_IDLE:
		break;

	/*
	 * Wait until logging command is completed,
	 * then sent out ACK to master.
	 */
	case RFID_LOG_ST_WAIT4CMD:
		if (log_busy())
			break;

		mcom_prepare_ack_response(&rfid_resp_ack_nak);
		fl_send_ack_nak = TRUE;

		rfid_log_st = RFID_LOG_ST_IDLE;
		break;

	/* Get the first logged record */
	case RFID_LOG_ST_GET_FIRST:
	/* Get next logged record */
	case RFID_LOG_ST_GET_NEXT:
		err = log_get_record(rfid_log_st == RFID_LOG_ST_GET_FIRST,
		    &rec, RFID_LOG_RECORD_SIZE);

		/* The record is not available yet - try again later. */
		if (err == EAGAIN)
			break;

		/* module BUSY, abort the process */
		if (err == EBUSY) {
			mcom_prepare_nak_response(&rfid_resp_ack_nak, err);
			fl_send_ack_nak = TRUE;

			rfid_log_st = RFID_LOG_ST_IDLE;
			break;
		}

		/* All records read, send the terminating packet. */
		if (rec == NULL) {
			/* Send the message to the master. */
			rfid_send_data_response_to_master("00", rec, 0);
		} else {
			/*
			 * Send the record to the master - discard \r\n
			 * terminating characters.
			 */
			rfid_send_data_response_to_master("00", rec,
			    strlen(rec) - 2);
		}

		rfid_log_st = RFID_LOG_ST_IDLE;

		break;
	} /* switch */

	return 0;
}

/*
 * Main service routine - called periodically from timer interrupt.
 */
static int
rfid_svc(void)
{
	/* Handle heartbeat LED */
	rfid_heartbeat_svc();

	/* Handle communication with RFID reader */
	rfid_id20_svc();

	/* Handle communication with logging service */
	rfid_log_svc();

	/* Handle communication with master */
	rfid_comm_svc();

	return 0;
}

/* global functions */
/*
 * Initialization routine.
 */
int
rfid_init(void)
{
	/*
	 * initialize IO pins
	 */
	/* set Tx to inactive before it is configured as digital output */
	LATDCLR = RFID_TX_MASK;
	TRISDCLR = RFID_TX_MASK;

	/*
	 * initialize UART1 used for communication with RFID reader
	 *  - baudrate: RFID_BAUDRATE
	 *  - 8 data bits, 1 stop bit
	 *  - continue to operate in IDLE mode
	 *  - IrDA mode disabled
	 *  - HW flow control turned off
	 *  - loopback mode disabled
	 *  - autobaud mode disabled
	 *  - standard speed mode - 16x baud clock enabled
	 */
	U1BRG = (SYS_FREQ / 16 / RFID_BAUDRATE - 1);
	U1MODE = UART_EN;

	/* TX pin is inverted, so configure it as active in LOW. */
	U1STA = (UART_RX_ENABLE | UART_TX_ENABLE | UART_TX_INV);

	/* Initialize state machine handling communication with ID20. */
	rfid_id20_st = RFID_ID20_ST_WAIT4STX;

	/* Initialize state machine handling communication with logging svc. */
	rfid_log_st = RFID_LOG_ST_IDLE;

	/* Enable passing RFID messages to master. */
	rfid_send2bt = TRUE;

	/* Register and enable RFID service. */
	rtl_svc_init("Rfid", rfid_svc, TRUE, FALSE);

	/* Make sure that message buffer is terminated with null character. */
	rfid_msg_buf[RFID_MSG_BUF_LN - 1] = '\0';

	/* Set module ID for purposes of communication with master. */
	rfid_resp_ack_nak.id = MCOMM_RFID_ID;
	rfid_resp_data.id = MCOMM_RFID_ID;

	/* Let LED indicator flash. */
	rfid_turn_led_on = FALSE;
	rfid_heartbeat_mode = HB_MODE_TICKS;

	/* Initialize packet sequence. */
	pkt_num = 1;

	return 0;
}

/*
 * Processes query from master targeted to RFID module
 */
int
rfid_process_query(char *data, int ln)
{
	int err = 0;
	BOOL respond_with_ack = TRUE;

	/* Assume ACK/NAK will be sent to master */
	fl_send_ack_nak = TRUE;

	/* Handle particular commands */
	switch (data[0]) {
		/* Enable passing RFID messages to master. */
		case MCOMM_RFID_CMD_SEND_ENABLE:
			rfid_send2bt = TRUE;
			break;

		/* Disable passing RFID messages to master. */
		case MCOMM_RFID_CMD_SEND_DISABLE:
			rfid_send2bt = FALSE;
			break;

		/* Send logged records to master. */
		case MCOMM_RFID_CMD_LOG_READ:
			/* Logging module currently busy */
			if (log_busy()) {
				err = MCOM_NAK_MODULE_BUSY;
				respond_with_ack = FALSE;

				break;
			}

			/* Determine which record is to be obtained. */
			switch (data[1]) {
			/* Get the first record. */
			case MCOMM_RFID_CMD_LOG_READ_FIRST:
				rfid_log_st = RFID_LOG_ST_GET_FIRST;

				/* Don't send ACK - data packet will follow. */
				fl_send_ack_nak = FALSE;

				break;

			/* Get next record. */
			case MCOMM_RFID_CMD_LOG_READ_NEXT:
				rfid_log_st = RFID_LOG_ST_GET_NEXT;

				/* Don't send ACK - data packet will follow. */
				fl_send_ack_nak = FALSE;

				break;

			/* Unknown subcommand. */
			default:
				err = MCOM_NAK_UNKNOWN_IDCMD;
				respond_with_ack = FALSE;

				break;
			} /* switch */

			break;

		/* Delete logged records. */
		case MCOMM_RFID_CMD_LOG_CLEAN:
			/* Kick off clean up process. */
			err = log_clean();

			if (err == 0) {
				/*
				 * Clean up process successfully commenced.
				 * ACK will be sent later from rfid_log_svc()
				 * after the process is completed.
				 */
				fl_send_ack_nak = FALSE;

				rfid_log_st = RFID_LOG_ST_WAIT4CMD;
			} else {
				respond_with_ack = FALSE;
			}

			break;

		/* unknown command */
		default:
			err = MCOM_NAK_UNKNOWN_IDCMD;
			respond_with_ack = FALSE;

			break;
	}

	/* ACK/NAK will be sent later */
	if (!fl_send_ack_nak)
		return 0;

	/*
	 * Send response to master indicating that command was processed.
	 */
	if (respond_with_ack) {
		mcom_prepare_ack_response(&rfid_resp_ack_nak);
	} else {
		mcom_prepare_nak_response(&rfid_resp_ack_nak, err);
	}

	return 0;
}

