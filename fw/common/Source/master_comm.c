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

#ifdef DAQ_MODULE
#include "data_acq.h"
#endif

#ifdef CANHAL_MODULE
#include "can_hal.h"
#endif

#ifdef CARMON_MODULE
#include "carmon.h"
#endif

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
#include "usb_app.h"
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
	int	(*putchar)(unsigned char, BOOL); /* sends char to master */

	/* receive buffer */
	char rx_buf[MCOM_PACKET_MAXLN];
	/* rx buffer tail  */
	unsigned char rx_tail;
	/* flag indicating that buffer contains received packet */
	BOOL fl_packet_received;

	/* pointer to response currently transmitted to master */
	mcom_response_t *resp;
} mcom_co;

/* local buffer for handling response to master */
static mcom_response_t mcom_resp;

/* flag indicating desire to suspend the system */
static BOOL mcom_suspend_fl;

/* main state machine */
static int mcom_st;
/* state machine taking care of receiving packet from master */
static int mcom_st_rx;
/* state machine taking care of transmitting packet to master */
static int mcom_st_tx;

/* TRUE indicates that packets are protected by CRC checksum */
static BOOL mcom_use_chsum;

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
	 * It is assumed after power on that master is to be connected via USB.
	 * However, it can also connect via Bluetooth. In that case, we need to
	 * redirect 'putchar' and 'getchar' routines appropriately.  
	 * Though in order to prevent collisions, the switch is done when we are
	 * waiting for a new packet.
	 * And once we switch to Bluetooth, we never switch back to USB.
	 */
	if (mcom_co.getchar != bt_getchar_master) {
		unsigned char dummy;

		/*
		 * Need to manually handle Bluetooth receiver state machine
		 * as long as operating in USB mode.
		 * Drain receive FIFO to make sure no character is lost.
		 */
		while (bt_getchar_master(&dummy) == 0)
			;

		if (bt_is_paired_with_master() &&
		    ((mcom_co.getchar)(&ch) != 0) &&
		    (mcom_st_rx == MCOM_ST_RX_W4SOF)) {
			mcom_co.getchar = bt_getchar_master;
			mcom_co.putchar = bt_putchar;
		}
	}

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
	int ln = mcom_co.rx_tail;

	/*
	 * Validate CRC checksum. <Pc...> packet is an exception, since
	 * it serves as a helper to calculate CRC-CCITT checksum
	 * for specified data.
	 */
	if (mcom_use_chsum && !(mcom_co.rx_buf[0] == MCOMM_PIC_ID &&
	    mcom_co.rx_buf[1] == MCOMM_PCORE_CMD_CALCCRC)) {
		unsigned short crc;
		unsigned short chsum = hex2ulong(mcom_co.rx_buf + ln - 4, 4);

		/* Verify we have at least received CRC and some data. */
		if (ln < 5) {
			mcom_prepare_nak_response(&mcom_resp,
			    MCOM_NAK_CRC_ERROR);

			return 1;
		}

		/* Calculate checksum. */
		crc = crc_calc_array(mcom_co.rx_buf, ln - 4);

		/* Verify checksum. */
		if (chsum != crc) {
			mcom_prepare_nak_response(&mcom_resp,
			    MCOM_NAK_CRC_ERROR);

			return 1;
		}

		/* Checksum processed, discard it. */
		ln -= 4;
	}

	/* Discard the first character, since it is to be processed here. */
	ln--;

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

#ifdef CANHAL_MODULE
	/* CAN module */
	case MCOMM_CAN_ID:
		err = can_process_query(&mcom_co.rx_buf[1], ln);

		if (err != 0) {
			mcom_prepare_nak_response(&mcom_resp, err);
			ret = 1;
		}

		break;
#endif

#ifdef CARMON_MODULE
	/* Carmon logger */
	case MCOMM_CM_ID:
		err = cm_process_query(&mcom_co.rx_buf[1], ln);

		if (err != 0) {
			mcom_prepare_nak_response(&mcom_resp, err);
			ret = 1;
		}

		break;
#endif

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

#ifdef DAQ_MODULE
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
#endif

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
				    MCOM_DATA_TYPE_BYTE, 1,
				    MCOM_DONT_FRAGMENT);
			} else {
				/*
				 * Malformed character received, send also
				 * error bits.
				 */
				data[0] = kwp2k_ch.ch;
				data[1] = kwp2k_ch.sts;

				mcom_prepare_generic_response(&mcom_resp,
				    MCOM_RESP_RXERR, data, MCOM_DATA_TYPE_BYTE,
				    2, MCOM_DONT_FRAGMENT);
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
				    MCOM_DATA_TYPE_BYTE, ln,
				    MCOM_DONT_FRAGMENT);
			} else {
				data[0] = (rx_err >> 8) & 0xff;
				data[1] = rx_err & 0xff;

				/* error occurred */
				mcom_prepare_generic_response(&mcom_resp,
				    MCOM_RESP_NAK, data,
				    MCOM_DATA_TYPE_BYTE, 2,
				    MCOM_DONT_FRAGMENT);
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
	 * Queue packet response to master for transmission,
	 * then allow receiving another packet from master.
	 */
	case MCOM_ST_SEND2MASTER:
		mcom_queue_response(&mcom_resp);

		mcom_st = MCOM_ST_W4PACKET;

		break;
	} /* switch */

	return 0;
}

/*
 * Transmit packet to master - one character at a time.
 *
 * Returns 0 if character was queued for transmission; EAGAIN otherwise.
 */
static int
mcom_tx_st(void)
{
	static unsigned char ch;
	static char tmp_buf[4];
	static unsigned int item_ln, item_index, buf_ln, buf_index;
	static unsigned short crc;
	unsigned int d;
	mcom_response_t *r;

	/*
	 * Module idle. Return EAGAIN to indicate that we are not processing
	 * the data. That assures caller does not end up looping if assuming
	 * that the transmitter is operating sending out characters.
	 */
	if (mcom_st_tx == MCOM_ST_TX_IDLE) {
		return EAGAIN;
	}

	/* Handle first character to be sent. */
	if (mcom_st_tx == MCOM_ST_TX_SOF) {
		ch = MCOM_PKT_SOF;
	}

	/*
	 * Try to queue the character for transmission.
	 * Do not block.
	 */
	if ((mcom_co.putchar)(ch, FALSE) != 0) {
		return EAGAIN;
	} 

	/* Re-calculate CRC. */
	crc_calc(&crc, ch);

	switch (mcom_st_tx) {
	/* machine is IDLE */
	case MCOM_ST_TX_IDLE:
		break;

	/* send SOF */
	case MCOM_ST_TX_SOF:
		/* Initialize CRC calculator. */
		crc_init(&crc);

		buf_index = 0;

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
		 * cur_pos points to start of next fragment in data buffer.
		 */

		buf_index = mcom_co.resp->cur_pos;

		/*
		 * NULL data pointer or zero data length indicate there
		 * is no data payload.
		 */
		if ((mcom_co.resp->data == NULL) ||
		    (mcom_co.resp->data_ln == 0)) {
			if (mcom_use_chsum) {
				/* send the checksum */
				item_index = 0;
				item_ln = 4;
				crc_finalize(&crc);
				uint2hex(crc, tmp_buf, 2);

				ch = tmp_buf[0];
				mcom_st_tx = MCOM_ST_TX_CHSUM;
			} else {
				/* send EOF */
				ch = MCOM_PKT_EOF;
				mcom_st_tx = MCOM_ST_TX_EOF;
			}

			break;
		}

		/*
		 * Adjust data length according to data type.
		 * Convert first data item to sequence of ASCII characters.
		 * Get first character to be sent.
		 */
		if (mcom_co.resp->data_type == MCOM_DATA_TYPE_ASCII) {
			ch = ((char *)mcom_co.resp->data)[buf_index];

			item_ln = 1;
		} else if (mcom_co.resp->data_type == MCOM_DATA_TYPE_BYTE) {
			d = ((unsigned char *)mcom_co.resp->data)[buf_index];

			item_ln = 2;
		} else if (mcom_co.resp->data_type == MCOM_DATA_TYPE_WORD) {
			d = ((unsigned int *)mcom_co.resp->data)[buf_index];

			item_ln = 4;
		}

		/* Convert data to ASCII if necessary. */
		if (mcom_co.resp->data_type != MCOM_DATA_TYPE_ASCII) {
			uint2hex(d, tmp_buf, item_ln / 2);
			ch = tmp_buf[0];
		}

		item_index = 0;

		/*
		 * For fragmented response, 'chunk_size' number of words
		 * is sent at once. Last fragment may contain less than
		 * 'chunk_size' words.
		 */
		if (mcom_fragment_response(mcom_co.resp)) {
			int remains = mcom_co.resp->data_ln -
			    mcom_co.resp->cur_pos;

			buf_ln = remains >= mcom_co.resp->chunk_size ?
				mcom_co.resp->chunk_size : remains;
		} else {
			buf_ln = mcom_co.resp->data_ln;
		}

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
			if (mcom_use_chsum) {
				/* send the checksum */
				item_index = 0;
				item_ln = 4;
				crc_finalize(&crc);
				uint2hex(crc, tmp_buf, 2);

				ch = tmp_buf[0];
				mcom_st_tx = MCOM_ST_TX_CHSUM;
			} else {
				/* send EOF */
				ch = MCOM_PKT_EOF;
				mcom_st_tx = MCOM_ST_TX_EOF;
			}

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

		/* Checksum sent, proceed with EOF */
		ch = MCOM_PKT_EOF;
		mcom_st_tx = MCOM_ST_TX_EOF;

		break;

	/* send EOF */
	case MCOM_ST_TX_EOF:
		/* Save pointer to just processed response. */
		r = mcom_co.resp;

		/* Remove processed response from the queue. */
		mcom_co.resp = r->next;
		r->next = NULL;

		/*
		 * If response is sent in fragments and this is not
		 * the last fragment, prepare next fragment and put it
		 * at the end of the list.
		 * That guarantees that other modules can send their
		 * responses as well.
		 */
		if (mcom_fragment_response(r) && !mcom_is_last_fragment(r)) {
			r->cur_pos += r->chunk_size;

			mcom_queue_response(r);
		}

		/* 
		 * Continue with sending until all packets
		 * in the queue are transmitted.
		 */
		if (mcom_co.resp == NULL) {
			mcom_st_tx = MCOM_ST_TX_IDLE;
		} else {
			mcom_st_tx = MCOM_ST_TX_SOF;
		}

		/*
		 * Transmission of the packet completed.
		 * Return EAGAIN which preempt caller and provides other
		 * services with their time quanta.
		 */
		return EAGAIN;

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
	int	i;

	/* Receive packet from master */
	mcom_rx_st();

	/* Process a packet received from master. */
	mcom_process_packet_st();

	/*
	 * Transmit packet to master. Loop as long as transmitter
	 * accepts data to be sent.
	 * Limit number of loops though to be on a safe side.
	 * 64 was picked up, since that is max size of data payload
	 * which can be sent via USB bulk transfer.
	 */
	for (i = 0; i < 64; i++) {
		if (mcom_tx_st() != 0) {
			break;
		}
	}

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

	/*
	 * Methods for receiving and sending char from/to master.
	 * Initially assume master is connected via USB CDC ACM.
	 */
	mcom_co.getchar = usbsacm_getchar;
	mcom_co.putchar = usbsacm_putchars;

	/* receive buffer */
	/* rx buffer tail  */
	mcom_co.rx_tail = 0;
	/* flag indicating that buffer contains received packet */
	mcom_co.fl_packet_received = FALSE;

	/* pointer to current response to master */
	mcom_co.resp = NULL;

	/* local structure holding response to master - unset module ID */
	mcom_resp.id = MCOMM_COMM_ID;

	/* receiver - wait for packet from master */
	mcom_st_rx = MCOM_ST_RX_W4SOF;
	/* transmitter idle */
	mcom_st_tx = MCOM_ST_TX_IDLE;
	/* main state machine - wait for packet from master */
	mcom_st = MCOM_ST_W4PACKET;

	/* Do not suspend */
	mcom_suspend_fl = FALSE;

	/* Support for CRC checksum disabled by default. */
	mcom_use_chsum = FALSE;

	/* Register service and enable it. */
	rtl_svc_init("MasterCom", mcom_svc, TRUE, FALSE);

	return 0;
}

/*
 * Initialize response structure.
 */
int
mcom_response_init(mcom_response_t *resp, char module_id)
{
	resp->id = module_id;
	
	resp->next = NULL;

	return 0;
}

/*
 * Put response to master to the end of transmit queue.
 */
int
mcom_queue_response(mcom_response_t *resp)
{
	mcom_response_t *r;

	if (mcom_fragment_response(resp)) {
		/*
		 * Increment fragment number. Last fragment has fragment
		 * number set to zero.
		 */
		if (mcom_is_last_fragment(resp)) {
			resp->fragment_number = 0;
		} else {
			resp->fragment_number++;

			if (resp->fragment_number > 255) {
				resp->fragment_number = 1;
			}
		}

		/*
		 * Header of fragmented message carries fragment number.
		 * Caller is responsible for allocating necessary writable
		 * space which can hold at least two character long
		 * null-terminated string.
		 */
		snprintf(resp->header, 3, "%02X", resp->fragment_number);
	}

	/*
	 * Response is to be put at the end of the queue, so clear pointer
	 * to the next.
	 */
	resp->next = NULL;

	/*
	 * If transmitter is idle, put the response to the beginning
	 * of the queue and kick off transmission process.
	 */
	if (mcom_co.resp == NULL) {
		mcom_co.resp = resp;
		mcom_st_tx = MCOM_ST_TX_SOF;

		return 0;
	}

	/* Find the end of the queue and put the response there. */
	for (r = mcom_co.resp; r->next != NULL; r = r->next)
		;

	r->next = resp;

	return 0;
}

/*
 * Send generic response to master.
 * Return EAGAIN if structure already contains data to be transmitted.
 */
int
mcom_prepare_generic_response(mcom_response_t *resp, char *header, void *data,
    int dtype, int dln, int chunk_size)
{
	/*
	 * If structure holds data which have not yet been sent to master,
	 * make sure not to overwrite them.
	 */
	if (mcom_response_inuse(resp))
		return EAGAIN;

	/* pointer to packet header */
	resp->header = header;
	/* pointer to data payload */
	resp->data = data;
	/* data type */
	resp->data_type = dtype;
	/* data length */
	resp->data_ln = dln;
	/* chunk size */
	resp->chunk_size = chunk_size;

	/* Reset buffer pointer - used when sending fragmented response. */
	resp->cur_pos = 0;

	/* Reset fragment number. */
	resp->fragment_number = 0;

	return 0;
}

/*
 * Send ACK response to master.
 */
int
mcom_prepare_ack_response(mcom_response_t *resp)
{
	return mcom_prepare_generic_response(resp, MCOM_RESP_ACK, NULL,
	    MCOM_DATA_TYPE_BYTE, 0, MCOM_DONT_FRAGMENT);
}

/*
 * Send NAK response to master.
 */
int
mcom_prepare_nak_response(mcom_response_t *resp, unsigned char err)
{
	static unsigned char e;

	e = err;
	return mcom_prepare_generic_response(resp, MCOM_RESP_NAK, &e,
	    MCOM_DATA_TYPE_BYTE, 1, MCOM_DONT_FRAGMENT);
}

/*
 * Returns TRUE if given response structure is in use; FALSE otherwise.
 *
 * Structure is in use if it is queued to be sent to master.
 * Transmit queue is assumed to be really short, so walking the whole queue
 * is not supposed to take very long.
 */
BOOL
mcom_response_inuse(mcom_response_t *resp)
{
	mcom_response_t *r;

	for (r = mcom_co.resp; (r != NULL) && (r != resp); r = r->next)
		;

	return (r == resp);
}

/*
 * Translate error codes from underlying modules to error
 * codes understood by master.
 */
int
mcom_err_translate(int err_in)
{
	int err_out;

	switch (err_in) {
	case EINVAL:
		err_out = MCOM_NAK_EINVAL;
		break;

	case EBUSY:
		err_out = MCOM_NAK_MODULE_BUSY;
		break;

	case EIO:
		err_out = MCOM_NAK_IO_ERROR;
		break;

	default:
		err_out = err_in;
		break;
	} /* switch */

	return err_out;
}

/*
 * Enable/disable packet checksum.
 */
void
mcom_chsum_configure(BOOL b)
{
	mcom_use_chsum = b;
}
