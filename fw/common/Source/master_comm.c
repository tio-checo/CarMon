/*********************************************************************
 *
 * Handles communication with master.
 *
 * structure of command message sent by master
 * --------------------------------
 * | < | Cmd |  ... data ...  | > |
 * --------------------------------
 *
 * structure of response
 * --------------------------------
 * | < | Rsp |  ... data ...  | > |
 * --------------------------------
 * 
 *********************************************************************/

#include "HardwareProfile.h"
#include "GenericTypeDefs.h"
#include "adc10.h"
#include "uart.h"
#include "xc.h"

#include "bt.h"
#include "data_acq.h"

#ifdef KWP2K_MODULE
#include "kwp2k.h"
#endif

#include "master_comm.h"
#include "mon.h"

#ifdef OBD_MODULE
#include "obd.h"
#endif

#include "pic_core.h"

#ifdef RFID_MODULE
#include "rfid.h"
#endif

#include "rt_light.h"
#include "utils.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

/* mcom_st states - processes packet received from master */
#define MCOM_ST_IDLE		0	/* machine IDLE */
#define MCOM_ST_W4PACKET	1	/* wait for packet from master */
#define MCOM_ST_SEND2ECU	2	/* transferring packet to ECU */
#define MCOM_ST_WAIT4ECU	3	/* wait for response from ECU */
#define MCOM_ST_SEND2MASTER	4	/* packet being sent to master */

/* mcom_st_rx states - receives packet from master */
#define MCOM_ST_RX_IDLE		0	/* machine is IDLE */
#define MCOM_ST_RX_W4SOF	1	/* waits for 'start of frame' */
#define MCOM_ST_RX_DATA		2	/* receives data */

/* mcom_st_tx states - transmits packet to master */
/* machine IDLE waiting for new packet to be transmitted */
#define MCOM_ST_TX_IDLE		0
#define	MCOM_ST_TX_SOF		1	/* send 'start of frame' */
#define	MCOM_ST_TX_ID		2	/* send module id */
#define MCOM_ST_TX_HEADER	3	/* send header */
#define MCOM_ST_TX_DATA		4	/* send data payload */
#define MCOM_ST_TX_CHSUM	5	/* send checksum */
#define MCOM_ST_TX_EOF		6	/* send 'end of frame' */

/* packet format */
#define MCOM_PKT_SOF		'<'	/* start of frame */
#define MCOM_PKT_EOF		'>'	/* end of frame */

/* max length of packet */
#define MCOM_PACKET_MAXLN	4096

/* local variables */
/*
 * Structure encapsulating data and methods participating in communication
 * with master.
 */
static struct mcom_comm_object_t {
	int	(*getchar)(unsigned char *);	/* receives char from master */
	int	(*putchar)(unsigned char);	/* sends char to master */

	/* receive buffer */
	char rx_buf[MCOM_PACKET_MAXLN];
	/* rx buffer tail  */
	unsigned char rx_tail;
	/* flag indicating that buffer contains received packet */
	BOOL fl_packet_received;

	/* pointer to response currently transmitted to master */
	mcom_response_t *resp;

	/* flag indicating that packet is being sent to master */
	BOOL fl_send_packet;
} mcom_co;

/* local buffer for handling response to master */
static mcom_response_t mcom_resp;

/* flag indicating desired to suspend the system */
static BOOL mcom_suspend_fl;

/* main state machine */
static int mcom_st;
/* state machine taking care of receiving packet from master */
static int mcom_st_rx;
/* state machine taking care of transmitting packet to master */
static int mcom_st_tx;

/* local functions */
/* Quit the application, jump to bootloader. */
static void mcom_jump_to_bootloader(void);

/*
 * Quit the application, jump to bootloader.
 */
static void
mcom_jump_to_bootloader(void)
{
	unsigned int d;

	/* disable interrupts first */
	asm volatile("di");

	/* Suspend BT module */
	bt_suspend();

	/* write invalid key to force lock */
	SYSKEY = 0x00000000;
	/* Key 1 */
	SYSKEY = 0xAA996655;
	/* Key 2 */
	SYSKEY = 0x556699AA;

	/* set SWRST bit to arm reset */
	RSWRSTSET = 1;
	/* read RSWRST register to trigger reset */
	d = RSWRST;
	/*
	 * prevent any unwanted code execution
	 * until reset occurs
	 */
	while(1);
}

/*
 * Receive packet from master.
 * Packet is in following form: '<[cmd][data...]>'.
 * It is assumed that previous packet has been processed, thus receive
 * buffer is available and can be overwritten.
 */
static int
mcom_rx_st(void)
{
	unsigned char ch;

	/*
	 * Attempt to completely dry out HW receive buffer.
	 * Cancel this initiative once the whole packet has been received.
	 * Flag has to be checked first, so that character in receive
	 * buffer is kept for another round in case full packet has just been
	 * received.
	 */
	while (!mcom_co.fl_packet_received &&
	    (mcom_co.getchar)(&ch) == 0) {

		switch (mcom_st_rx) {
		/* machine is IDLE */
		case MCOM_ST_RX_IDLE:
		break;

		/* wait for 'start of frame' */
		case MCOM_ST_RX_W4SOF:
			if (ch == MCOM_PKT_SOF) {
				mcom_co.rx_tail = 0;
				mcom_st_rx = MCOM_ST_RX_DATA;
			}
			break;

		/* receive data */
		case MCOM_ST_RX_DATA:
			/*
			 * Complete packet received, signal it
			 * and wait for next one.
			 */
			if (ch == MCOM_PKT_EOF) {
				mcom_co.fl_packet_received = TRUE;
				mcom_st_rx = MCOM_ST_RX_W4SOF;
			} else if (mcom_co.rx_tail >= MCOM_PACKET_MAXLN) {
				/*
				 * Packet would not fit in a buffer,
				 * discard it.
				 */
				mcom_st_rx = MCOM_ST_RX_W4SOF;
			} else {
				/* store received character in a buffer */
				mcom_co.rx_buf[mcom_co.rx_tail++] = ch;
			}

			break;
		} /* switch */
	} /* while */

	return 0;
}

/*
 * Decode received packet.
 * Returns:
 *   0 - packet has been successfully dispatched to target module for further
 *       processing.
 *   1 - there has been response prepared which is to be sent to master.
 */
static int
mcom_decode_packet(void)
{
#ifdef KWP2K_MODULE
	/* packet to be sent to ECU */
	static unsigned char mcom_packet_ecu[MCOM_PACKET_MAXLN];

	BOOL send_wup;
	int j;
#endif

	int err;
	int ret = 0;
	int ln = mcom_co.rx_tail - 1;

	/* dispatch packet to appropriate handle routines based on module ID */
	switch (mcom_co.rx_buf[0]) {
	/* PIC core module */
	case MCOMM_PIC_ID:
		err = pcore_process_query(&mcom_co.rx_buf[1], ln);

		if (err != 0) {
			mcom_prepare_nak_response(&mcom_resp, err);
			ret = 1;
		}

		break;

#ifdef RFID_MODULE
	/* RFID module */
	case MCOMM_RFID_ID:
		err = rfid_process_query(&mcom_co.rx_buf[1], ln);

		if (err != 0) {
			mcom_prepare_nak_response(&mcom_resp, err);
			ret = 1;
		}

		break;
#endif

	/* A/D converter */
	case MCOMM_ADC_ID:
		err = daq_adc_process_query(&mcom_co.rx_buf[1], ln);

		if (err != 0) {
			mcom_prepare_nak_response(&mcom_resp, err);
			ret = 1;
		}

		break;

	/* data acquisition module */
	case MCOMM_DAQ_ID:
		err = daq_acq_process_query(&mcom_co.rx_buf[1], ln);

		if (err != 0) {
			mcom_prepare_nak_response(&mcom_resp, err);
			ret = 1;
		}

		break;

#ifdef OBD_MODULE
	/* module handling communication with ECUs */
	case MCOMM_ECU_ID:
		err = obd_process_query(&mcom_co.rx_buf[1], ln);

		if (err != 0) {
			mcom_prepare_nak_response(&mcom_resp, err);
			ret = 1;
		}

		break;
#endif

	/* now process legacy packets */

#ifdef KWP2K_MODULE
	/*
	 * Send raw KWP packet prepended with Wake up Pattern.
	 * This command is only supported in "basic" mode.
	 */
	case MCOM_CMD_SEND_WUP:
		if (!kwp2k_is_basic_mode()) {
			mcom_prepare_nak_response(&mcom_resp,
			    MCOM_NAK_UNKNOWN_IDCMD);

			ret = 1;

			break;
		}
	/* Send packet to ECU. */
	case MCOM_CMD_SEND:
		/* WuP should be sent out */
		send_wup = mcom_co.rx_buf[0] == MCOM_CMD_SEND_WUP;

		/* convert data from ASCII to raw hex */
		/* length of data payload - number of bytes */
		j = ln / 2;
		hex2ucharbuf(&mcom_co.rx_buf[1], mcom_packet_ecu, j * 2);

		/* If operating in basic mode, send packet directly to ECU */
		if (kwp2k_is_basic_mode()) {
			if (kwp2k_ll_send(mcom_packet_ecu, j, send_wup, FALSE)
			    == 0) {
				/*
				 * wait for packet to be completely
				 * transmitted to ECU.
				 */
				mcom_st = MCOM_ST_SEND2ECU;
			} else {
				mcom_prepare_nak_response(&mcom_resp,
				    MCOM_NAK_KWP2K_TX_ERR);

				ret = 1;
			}
		} else {
			/* enhanced mode */
			if (kwp2k_ul_send(mcom_packet_ecu, j) == 0) {
				/* wait for ECU response */
				mcom_st = MCOM_ST_W4PACKET;
			} else {
				mcom_prepare_nak_response(&mcom_resp,
				   MCOM_NAK_KWP2K_TX_ERR);

				ret = 1;
			}
		}

		break;

	/* Change communication mode */
	case MCOM_CMD_CHANGE_MODE:
		/*
		 * Only change the mode if underlying communication module
		 * is idle.
		 */
		if (kwp2k_is_idle()) {
			if (kwp2k_set_mode(hex2ulong(&mcom_co.rx_buf[1],
			    2)) == 0) {
				mcom_prepare_ack_response(&mcom_resp);
			} else {
				mcom_prepare_nak_response(&mcom_resp,
				    MCOM_NAK_EINVAL);
			}
		} else {
			mcom_prepare_nak_response(&mcom_resp,
			    MCOM_NAK_KWP2K_BUSY);
		}

		ret = 1;

		break;
#endif

	/* Enter diagnostic mode */
	case MCOM_CMD_DIAG_MODE:
		err = mon_diag_enter(mcom_co.rx_buf[1]);
		if (err != 0) {
			mcom_prepare_nak_response(&mcom_resp,
			    MCOM_NAK_EINVAL);

			ret = 1;
		}
		break;

	/* Go to sleep */
	case MCOM_CMD_SLEEP:
		mcom_suspend_fl = TRUE;
		break;

	/* Quit the application, jump to bootloader */
	case MCOM_CMD_QUIT:
		mcom_jump_to_bootloader();

		/* NOT REACHED */
		break;

	/* should not happen, but... */
	default:
		mcom_prepare_nak_response(&mcom_resp,
		    MCOM_NAK_UNKNOWN_IDCMD);
		ret = 1;

		break;
	} /* switch */

	return ret;
}

/*
 * Main state machine
 *  - processes packet received from master
 *  - assembles response packet
 */
static int
mcom_process_packet_st(void)
{
#ifdef KWP2K_MODULE
	static unsigned char data[2];

	kwp2k_rx_char_t kwp2k_ch;
#endif

	switch (mcom_st) {
	case MCOM_ST_IDLE:
		break;

	/*
	 * Wait for packet from master. Meanwhile, process packets
	 * received from ECU.
	 */
	case MCOM_ST_W4PACKET:
		if (mcom_co.fl_packet_received) {
			/*
			 * Decode received packet and dispatch it to
			 * the appropriate module for further processing.
			 * If there has been packet prepared during
			 * the process, send it to the master.
			 */
			if (mcom_decode_packet() == 1) {
				/* Queue response for transmission */
				mcom_st = MCOM_ST_SEND2MASTER;
			}

			/* wait for another packet from master */
			mcom_co.fl_packet_received = FALSE;

			break;
		}

#ifdef KWP2K_MODULE
		/* Handle packet received from ECU */
		/* In basic mode, send every byte received from ECU */
		if (kwp2k_is_basic_mode() && (kwp2k_getchar(&kwp2k_ch) == 0)) {
			/*
			 * Inspect received character and build packet
			 * which will be sent to master.
			 */
			if (kwp2k_ch.sts == 0) {
				/* valid character received */
				data[0] = kwp2k_ch.ch;

				mcom_prepare_generic_response(&mcom_resp,
				    MCOM_RESP_DATA, data,
				    MCOM_DATA_TYPE_BYTE, 1);
			} else {
				/*
				 * Malformed character received, send also
				 * error bits.
				 */
				data[0] = kwp2k_ch.ch;
				data[1] = kwp2k_ch.sts;

				mcom_prepare_generic_response(&mcom_resp,
				    MCOM_RESP_RXERR, data, MCOM_DATA_TYPE_BYTE,
				    2);
			}
		} else {
			unsigned char *d;
			int rx_err, ln, ret;

			ret = kwp2k_ul_recv(&d, &ln, &rx_err);

			/* packet just being received */
			if (ret == EAGAIN)
				break;

			/* valid packet received */
			if (ret == 0) {
				mcom_prepare_generic_response(&mcom_resp,
				    MCOM_RESP_DATA, d,
				    MCOM_DATA_TYPE_BYTE, ln);
			} else {
				data[0] = (rx_err >> 8) & 0xff;
				data[1] = rx_err & 0xff;

				/* error occurred */
				mcom_prepare_generic_response(&mcom_resp,
				    MCOM_RESP_NAK, data,
				    MCOM_DATA_TYPE_BYTE, 2);
			}
		}

		/* Wait until response is queued for transmission */
		mcom_st = MCOM_ST_SEND2MASTER;
#endif
		break;


#ifdef KWP2K_MODULE
	/*
	 * Wait until packet is completely transmitted to ECU.
	 * Then (if in basic mode) send ACK to master. In enhanced mode,
	 * wait for response from ECU.
	 */
	case MCOM_ST_SEND2ECU:
		if (!kwp2k_transmitter_busy()) {
			if (kwp2k_is_basic_mode()) {
				mcom_prepare_ack_response(&mcom_resp);

				/*
				 * Wait until response is queued
				 * for transmission
				 */
				mcom_st = MCOM_ST_SEND2MASTER;
			} else {
				mcom_st = MCOM_ST_W4PACKET;
			}
		}

		break;
#endif

	/*
	 * Wait until packet response to master is queued for transmission,
	 * then allow receiving another packet from master.
	 */
	case MCOM_ST_SEND2MASTER:
		if (mcom_queue_response(&mcom_resp) == 0) {
			mcom_st = MCOM_ST_W4PACKET;
		}

		break;
	} /* switch */

	return 0;
}

/*
 * Transmit packet to master.
 */
static int
mcom_tx_st(void)
{
	static unsigned char ch;
	static char tmp_buf[4];
	static unsigned int item_ln, item_index, buf_ln, buf_index, chsum;
	unsigned int d;

	/*
	 * If module is sending out characters (not idle), wait until
	 * current character is queued for transmission.
	 */
	if ((mcom_st_tx != MCOM_ST_TX_IDLE) && ((mcom_co.putchar)(ch) != 0)) {
		return 0;
	}	 

	switch (mcom_st_tx) {
	/* machine is IDLE */
	case MCOM_ST_TX_IDLE:
		/*
		 * If there is a new packet to be sent, initiate transmission
		 * by sending SOF character.
		 */
		if (mcom_co.fl_send_packet) {
			ch = MCOM_PKT_SOF;

			mcom_st_tx = MCOM_ST_TX_SOF;
		}

		break;

	/* send SOF */
	case MCOM_ST_TX_SOF:
		buf_index = 0;
		chsum = 0;

		/*
		 * Continue with sending module ID. Skip if there is no
		 * module ID.
		 */
		if (mcom_co.resp->id != 0) {
			ch = mcom_co.resp->id;
			mcom_st_tx = MCOM_ST_TX_ID;
		} else {
			ch = mcom_co.resp->header[buf_index];
			mcom_st_tx = MCOM_ST_TX_HEADER;
		}

		break;

	/* send module ID */
	case MCOM_ST_TX_ID:
		/* Continue with sending a header */
		ch = mcom_co.resp->header[buf_index];
		mcom_st_tx = MCOM_ST_TX_HEADER;

		break;

	/* send packet header */
	case MCOM_ST_TX_HEADER:
		buf_index++;

		ch = mcom_co.resp->header[buf_index];

		if (ch != '\0')
			break;

		/*
		 * Complete header has been sent, proceed with sending
		 * data payload.
		 */

		buf_index = 0;

		/*
		 * NULL data pointer or zero data length indicate there
		 * is no data payload.
		 */
		if ((mcom_co.resp->data == NULL) || (mcom_co.resp->data_ln == 0)) {
			/* No data payload, proceed with checksum. */
			item_ln = 2;
			uint2hex(chsum, tmp_buf, 1);

			/* for now skip sending the checksum */
//			ch = tmp_buf[0];
//			mcom_st_tx = MCOM_ST_TX_CHSUM;

			ch = MCOM_PKT_EOF;
			mcom_st_tx = MCOM_ST_TX_EOF;

			break;
		}

		/*
		 * Adjust data length according to data type.
		 * Convert first data item to sequence of ASCII characters.
		 * Get first character to be sent.
		 */
		if (mcom_co.resp->data_type == MCOM_DATA_TYPE_ASCII) {
			ch = ((char *)mcom_co.resp->data)[0];

			item_ln = 1;
		} else if (mcom_co.resp->data_type == MCOM_DATA_TYPE_BYTE) {
			d = ((unsigned char *)mcom_co.resp->data)[0];

			item_ln = 2;
		} else if (mcom_co.resp->data_type == MCOM_DATA_TYPE_WORD) {
			d = ((unsigned int *)mcom_co.resp->data)[0];

			item_ln = 4;
		}

		/* Convert data to ASCII if necessary. */
		if (mcom_co.resp->data_type != MCOM_DATA_TYPE_ASCII) {
			uint2hex(d, tmp_buf, item_ln / 2);
			ch = tmp_buf[item_index];
		}

		item_index = 0;
		buf_ln = mcom_co.resp->data_ln;

		mcom_st_tx = MCOM_ST_TX_DATA;

		break;

	/* send data payload */
	case MCOM_ST_TX_DATA:
		/* Send next ASCII character of data item */
		if (++item_index < item_ln) {
			ch = tmp_buf[item_index];

			break;
		}

		/* Sending of data item completed, proceed with next one */
		buf_ln--;
		buf_index++;

		/* If all data has been sent, proceed with checksum */
		if (buf_ln == 0) {
			item_index = 0;
			item_ln = 2;
			uint2hex(chsum, tmp_buf, 1);

			/* for now skip sending the checksum */
//			ch = tmp_buf[0];
//			mcom_st_tx = MCOM_ST_TX_CHSUM;

			ch = MCOM_PKT_EOF;
			mcom_st_tx = MCOM_ST_TX_EOF;

			break;
		}

		/*
		 * Get next data item to be sent taking into account
		 * data type
		 */
		if (mcom_co.resp->data_type == MCOM_DATA_TYPE_ASCII) {
			ch = ((char *)mcom_co.resp->data)[buf_index];
		} else if (mcom_co.resp->data_type == MCOM_DATA_TYPE_BYTE) {
			d = ((unsigned char *)mcom_co.resp->data)[buf_index];
		} else if (mcom_co.resp->data_type == MCOM_DATA_TYPE_WORD) {
			d = ((unsigned int *)mcom_co.resp->data)[buf_index];
		}

		/* Convert data to ASCII if necessary. */
		if (mcom_co.resp->data_type != MCOM_DATA_TYPE_ASCII) {
			uint2hex(d, tmp_buf, item_ln / 2);
			ch = tmp_buf[0];
		}

		item_index = 0;

		break;

	/* send checksum */
	case MCOM_ST_TX_CHSUM:
		/* Send next ASCII character of checksum */
		if (++item_index < item_ln) {
			ch = tmp_buf[item_index];

			break;
		}

		/* All data has been sent, proceed with EOF */
		ch = MCOM_PKT_EOF;
		mcom_st_tx = MCOM_ST_TX_EOF;

		break;

	/* send EOF */
	case MCOM_ST_TX_EOF:
		/* wait for another packet to be sent */
		mcom_st_tx = MCOM_ST_TX_IDLE;

		/* signal that transmitter is ready again */
		mcom_co.fl_send_packet = FALSE;

		break;
	}

	return 0;
}

/*
 * Main service routine
 */
static int
mcom_svc(void)
{
	/* Receive packet from master */
	mcom_rx_st();

	/* Process a packet received from master. */
	mcom_process_packet_st();

	/* Transmit packet to master */
	mcom_tx_st();

	return 0;
}

/*
 * Check if suspend is required.
 */
BOOL
mcom_suspend(void)
{
	BOOL tmp = mcom_suspend_fl;

	mcom_suspend_fl = FALSE;

	return tmp;
}

/*
 * Initialize module
 */
int
mcom_init(void)
{
	/* Initialize main communication object */
 	/* method for receiving char from master */
	mcom_co.getchar = bt_getchar;
	/* method for sending char to master */
	mcom_co.putchar = bt_putchar;

	/* receive buffer */
	/* rx buffer tail  */
	mcom_co.rx_tail = 0;
	/* flag indicating that buffer contains received packet */
	mcom_co.fl_packet_received = FALSE;

	/* pointer to current response to master */
	mcom_co.resp = NULL;

	/* local structure holding response to master - unset module ID */
	mcom_resp.id = MCOMM_COMM_ID;

	/* flag indicating that response is to be sent to master */
	mcom_co.fl_send_packet = FALSE;

	/* receiver - wait for packet from master */
	mcom_st_rx = MCOM_ST_RX_W4SOF;
	/* transmitter idle */
	mcom_st_tx = MCOM_ST_TX_IDLE;
	/* main state machine - wait for packet from master */
	mcom_st = MCOM_ST_W4PACKET;

	/* Do not suspend */
	mcom_suspend_fl = FALSE;

	/* Register service and enable it. */
	rtl_svc_init("MasterCom", mcom_svc, TRUE, FALSE);

	return 0;
}

/*
 * Put response to master to the transmit queue.
 * If the queue is full, return EAGAIN, otherwise return 0.
 */
int
mcom_queue_response(mcom_response_t *resp)
{
	/* If there is currently response being sent, signal it */
	if (mcom_co.fl_send_packet) {
		return EAGAIN;
	}

	mcom_co.resp = resp;
	mcom_co.fl_send_packet = TRUE;

	return 0;
}

/*
 * Send generic response to master.
 */
int
mcom_prepare_generic_response(mcom_response_t *resp, char *header, void *data,
    int dtype, int dln)
{
	/* pointer to packet header */
	resp->header = header;
	/* pointer to data payload */
	resp->data = data;
	/* data type */
	resp->data_type = dtype;
	/* data length */
	resp->data_ln = dln;

	return 0;
}

/*
 * Send ACK response to master.
 */
int
mcom_prepare_ack_response(mcom_response_t *resp)
{
	mcom_prepare_generic_response(resp, MCOM_RESP_ACK, NULL,
	    MCOM_DATA_TYPE_BYTE, 0);

	return 0;
}

/*
 * Send NAK response to master.
 */
int
mcom_prepare_nak_response(mcom_response_t *resp, unsigned char err)
{
	static unsigned char e;

	e = err;
	mcom_prepare_generic_response(resp, MCOM_RESP_NAK, &e,
	    MCOM_DATA_TYPE_BYTE, 1);

	return 0;
}

