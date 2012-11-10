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
#include "kwp2k.h"
#include "master_comm.h"
#include "mon.h"
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
#define MCOM_ST_TX_IDLE		0	/* machine is IDLE */
#define MCOM_ST_TX_SOF		1	/* send 'start of frame' */
#define MCOM_ST_TX_DATA		2	/* send data and 'end of frame' */

/* packet format */
#define MCOM_PKT_SOF		'<'	/* start of frame */
#define MCOM_PKT_EOF		'>'	/* end of frame */

/* max length of packet */
#define MCOM_PACKET_MAXLN	260

/* local variables */
/*
 * Structure encapsulating data and methods participating in communication
 * with master.
 */
static struct mcom_comm_object_t {
	int	(*getchar)(unsigned char *);	/* receives char from master */
	int	(*putchar)(unsigned char);	/* sends char to master */

	/* receive buffer */
	unsigned char rx_buf[MCOM_PACKET_MAXLN];
	/* rx buffer tail  */
	unsigned char rx_tail;
	/* flag indicating that buffer contains received packet */
	BOOL fl_packet_received;

	/* transmit buffer */
	unsigned char tx_buf[MCOM_PACKET_MAXLN];
	/* tx buffer head  */
	unsigned char tx_head;
	/* number of characters to be sent */
	unsigned char tx_ln;
	/* flag indicating that packet is being sent to master */
	BOOL fl_send_packet;
} mcom_comm;

/* flag indicating desired to suspend the system */
static BOOL mcom_suspend_fl;

/* packet to be sent to ECU */
static unsigned char mcom_packet_ecu[MCOM_PACKET_MAXLN];

/* main state machine */
static int mcom_st;
/* state machine taking care of receiving packet from master */
static int mcom_st_rx;
/* state machine taking care of transmitting packet to master */
static int mcom_st_tx;

/* local functions */
/* send packet to master */
static int mcom_send_packet(unsigned char ln);
/* send response (ACK or NAK) to master along with optional data */
static int mcom_respond_to_master(char resp, unsigned char *data, int ln);
/* Quit the application, jump to bootloader. */
static void mcom_jump_to_bootloader(void);

/*
 * Send response (ACK or NAK) to master along with optional data.
 */
static int
mcom_respond_to_master(char resp, unsigned char *data, int ln)
{
	int res;

	/* response type - ACK, NAK, ... */
	mcom_comm.tx_buf[0] = resp;

	/* data are sent in ASCII format */
	hex2ascii(data, &mcom_comm.tx_buf[1], ln);

	/* Initiate process of sending packet to master */
	res = mcom_send_packet(1 + 2 * ln);

	/* wait until packet is completely transmitted */
	mcom_st = MCOM_ST_SEND2MASTER;

	return (res);
}

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
 * Go to sleep mode
 */
/*
 * Initialize module
 */
int
mcom_init(void)
{
	/* Initialize main communication object */
 	/* method for receiving char from master */
	mcom_comm.getchar = bt_getchar;
	/* method for sending char to master */
	mcom_comm.putchar = bt_putchar;

	/* receive buffer */
	/* rx buffer tail  */
	mcom_comm.rx_tail = 0;
	/* flag indicating that buffer contains received packet */
	mcom_comm.fl_packet_received = FALSE;

	/* transmit buffer */
	mcom_comm.tx_head = mcom_comm.tx_ln = 0;
	/* flag indicating that packet is being sent to master */
	mcom_comm.fl_send_packet = FALSE;

	/* receiver - wait for packet from master */
	mcom_st_rx = MCOM_ST_RX_W4SOF;
	/* transmitter idle */
	mcom_st_tx = MCOM_ST_TX_IDLE;
	/* main state machine - wait for packet from master */
	mcom_st = MCOM_ST_W4PACKET;

	/* Do not suspend */
	mcom_suspend_fl = FALSE;

	return MCOM_OK;
}

/*
 * Send packet to master
 */
static int
mcom_send_packet(unsigned char ln)
{
	/*
	 * If previous packet is still being sent, wait.
	 */
	if (mcom_comm.fl_send_packet) {
		return MCOM_TX_IN_PROGRESS;
	}

	mcom_comm.tx_ln = ln;
	mcom_comm.tx_head = 0;
	mcom_comm.fl_send_packet = TRUE;
	mcom_st_tx = MCOM_ST_TX_SOF;

	return MCOM_OK;
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
	while (!mcom_comm.fl_packet_received &&
	    (mcom_comm.getchar)(&ch) == MCOM_OK) {

		switch (mcom_st_rx) {
		/* machine is IDLE */
		case MCOM_ST_RX_IDLE:
		break;

		/* wait for 'start of frame'*/
		case MCOM_ST_RX_W4SOF:
			if (ch == MCOM_PKT_SOF) {
				mcom_comm.rx_tail = 0;
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
				mcom_comm.fl_packet_received = TRUE;
				mcom_st_rx = MCOM_ST_RX_W4SOF;
			} else if (mcom_comm.rx_tail >= MCOM_PACKET_MAXLN) {
				/*
				 * Packet would not fit in a buffer,
				 * discard it.
				 */
				mcom_st_rx = MCOM_ST_RX_W4SOF;
			} else {
				/* store received character in a buffer */
				mcom_comm.rx_buf[mcom_comm.rx_tail++] = ch;
			}

			break;
		} /* switch */
	} /* while */

	return MCOM_OK;
}

/*
 * Decode received packet.
 */
static int
mcom_decode_packet(void)
{
	BOOL send_wup;
	int j;
	unsigned long res;
	unsigned char data[2];
	unsigned char error;

	switch (mcom_comm.rx_buf[0]) {
	/* get value from AD converter */
	case MCOM_CMD_GET_AD:
		if (daq_get_adc_value(ascii2hex(&mcom_comm.rx_buf[1], 2),
		    &res) == 0) {
			data[0] = (res >> 8) & 0xff;
			data[1] = res & 0xff;

			mcom_respond_to_master(MCOM_RESP_ADC, data, 2);
		} else {
			error = MCOM_NAK_ADC_ERR;
			mcom_respond_to_master(MCOM_RESP_NAK, &error, 1);
		}
		break;

	/*
	 * Send raw KWP packet prepended with Wake up Pattern.
	 * This command is only supported in "basic" mode.
	 */
	case MCOM_CMD_SEND_WUP:
		if (!kwp2k_is_basic_mode()) {
			error = MCOM_NAK_UNKNOWN_CMD;
			mcom_respond_to_master(MCOM_RESP_NAK, &error, 1);

			break;
		}
	/* send packet to ECU */
	case MCOM_CMD_SEND:
		/* WuP should be sent out */
		send_wup = mcom_comm.rx_buf[0] == MCOM_CMD_SEND_WUP;

		/* convert data from ASCII to raw hex */
		/* length of data payload - number of bytes */
		j = (mcom_comm.rx_tail - 1) / 2;
		ascii2hexbuf(&mcom_comm.rx_buf[1], mcom_packet_ecu, j * 2);

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
				error = MCOM_NAK_KWP2K_TX_ERR;
				mcom_respond_to_master(MCOM_RESP_NAK, &error,
				    1);
			}
		} else {
			/* enhanced mode */
			if (kwp2k_ul_send(mcom_packet_ecu, j) == 0) {
				/* wait for ECU response */
				mcom_st = MCOM_ST_W4PACKET;
			} else {
				error = MCOM_NAK_KWP2K_TX_ERR;
				mcom_respond_to_master(MCOM_RESP_NAK, &error,
				    1);
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
			if (kwp2k_set_mode(ascii2hex(&mcom_comm.rx_buf[1],
			    2)) == 0) {
				mcom_respond_to_master(MCOM_RESP_ACK, NULL, 0);
			} else {
				error = MCOM_NAK_EINVAL;
				mcom_respond_to_master(MCOM_RESP_NAK, &error,
				    1);
			}
		} else {
			error = MCOM_NAK_KWP2K_BUSY;
			mcom_respond_to_master(MCOM_RESP_NAK, &error, 1);
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
		error = MCOM_NAK_UNKNOWN_CMD;
		mcom_respond_to_master(MCOM_RESP_NAK, &error, 1);

		break;
	} /* switch */

	return MCOM_OK;
}

/*
 * Main state machine
 *  - processes packet received from master
 *  - assembles response packet
 */
static int
mcom_process_packet_st(void)
{
	unsigned char *data;
	int ln;
	int ret;
	int rx_err;
	kwp2k_rx_char_t kwp2k_ch;

	switch (mcom_st) {
	case MCOM_ST_IDLE:
		break;

	/*
	 * Wait for packet from master. Meanwhile, process packets
	 * received from ECU.
	 */
	case MCOM_ST_W4PACKET:
		if (mcom_comm.fl_packet_received) {
			/*
			 * Decode received packet and transition
			 * to appropriate state.
			 */
			mcom_decode_packet();

			/* wait for another packet from master */
			mcom_comm.fl_packet_received = FALSE;

			break;
		}

		/* Handle packet received from ECU */
		/* In basic mode, send every byte received from ECU */
		if (kwp2k_is_basic_mode() && (kwp2k_getchar(&kwp2k_ch) == 0)) {
			unsigned char data[2];

			/*
			 * Inspect received character and build packet
			 * which will be sent to master.
			 */
			if (kwp2k_ch.sts == 0) {
				/* valid character received */
				data[0] = kwp2k_ch.ch;
				mcom_respond_to_master(MCOM_RESP_DATA,
				    data, 1);
			} else {
				/*
				 * Malformed character received, send also
				 * error bits.
				 */
				data[0] = kwp2k_ch.ch;
				data[1] = kwp2k_ch.sts;
				mcom_respond_to_master(MCOM_RESP_RXERR,
				    data, 2);
			}
		} else {
			ret = kwp2k_ul_recv(&data, &ln, &rx_err);

			/* packet just being received */
			if (ret == EAGAIN)
				break;

			/* valid packet received */
			if (ret == 0) {
				mcom_respond_to_master(MCOM_RESP_DATA, data,
				    ln);
			} else {
				unsigned char data[2];

				data[0] = (rx_err >> 8) & 0xff;
				data[1] = rx_err & 0xff;

				/* error occurred */
				mcom_respond_to_master(MCOM_RESP_NAK, data, 2);
			}
		}

		break;

	/*
	 * Wait until packet is completely transmitted to ECU.
	 * Then (if in basic mode) send ACK to master. In enhanced mode,
	 * wait for response from ECU.
	 */
	case MCOM_ST_SEND2ECU:
		if (!kwp2k_transmitter_busy()) {
			if (kwp2k_is_basic_mode())
				mcom_respond_to_master(MCOM_RESP_ACK, NULL, 0);
			else
				mcom_st = MCOM_ST_W4PACKET;
		}

		break;

	/*
	 * Wait for packet to be completely sent to master, then allow
	 * receiving another packet from master.
	 */
	case MCOM_ST_SEND2MASTER:
		if (!mcom_comm.fl_send_packet) {
			mcom_st = MCOM_ST_W4PACKET;
		}

		break;
	} /* switch */

	return MON_OK;
}

/*
 * Transmit packet to master.
 */
static int
mcom_tx_st(void)
{
	switch (mcom_st_tx) {
	/* machine is IDLE */
	case MCOM_ST_TX_IDLE:
		break;

	/* send SOF */
	case MCOM_ST_TX_SOF:
		if ((mcom_comm.putchar)(MCOM_PKT_SOF) == MCOM_OK)
			mcom_st_tx = MCOM_ST_TX_DATA;
		break;

	/* send data and EOF */
	case MCOM_ST_TX_DATA:
		/* all data sent, so EOF */
		if (mcom_comm.tx_head >= mcom_comm.tx_ln) {
			if ((mcom_comm.putchar)(MCOM_PKT_EOF) == MCOM_OK) {
				/*
				 * Packet has been completely transmitted
				 * to master, signal it.
				 */
				mcom_comm.fl_send_packet = FALSE;
				mcom_st_tx = MCOM_ST_TX_IDLE;
			}
		} else {
			/* send data */
			unsigned char c;

			c = mcom_comm.tx_buf[mcom_comm.tx_head];

			/* byte successfully sent out, move to the next one */
			if ((mcom_comm.putchar)(c) == MCOM_OK) {
				mcom_comm.tx_head++;
			}
		}
		break;
	}

	return MCOM_OK;
}

/*
 * Main service routine
 */
int
mcom_service(void)
{
//	kwp2k_rx_char_t	kwp2k_ch;

	/* Receive packet from master */
	mcom_rx_st();

	/* Process a packet received from master. */
	mcom_process_packet_st();

	/* Transmit packet to master */
	mcom_tx_st();	

	return MCOM_OK;
}

