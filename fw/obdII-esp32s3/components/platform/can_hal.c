/*********************************************************************
 *
 * Support for Controller Area Network (CAN) PIC module - provides
 * API for master device.
 *
 *********************************************************************/

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "GenericTypeDefs.h"

#include "master_comm.h"
#include "can_hal.h"
#include "can_snoop.h"
#include "can_twai.h"
#include "obd.h"
#include "rt_light.h"
#include "utils.h"

#include "esp_timer.h"

/* constants */
/* OBD specific definitions */
/* tester SID */
#define	CAN_OBD_TESTER_SID	0x7df
/* valid ECU SIDs - 0x7E8 - 0x7EF */
#define	CAN_OBD_ECU_SID		0x7e8
#define	CAN_OBD_ECU_SID_MSK	0x7f8
/* tester EID */
#define	CAN_OBD_TESTER_EID	0x18db33f1
/* 200ms CAN OBD receive timeout */
#define	CAN_OBD_RX_TIMEOUT	200000
/* 100ms CAN transmit timeout - must be shorter than CAN_OBD_RX_TIMEOUT */
#define	CAN_TX_TIMEOUT		100000

/* USDT specific definitions */
/*
 * Max size of USDT data payload.
 * USDT mechanism supports up to 4096 bytes,
 * though we currently enforce lower limit.
 */
#define	CAN_USDT_MAX_DATA_SIZE	128
/* Size of CAN frame data payload - always 8 per standard */
#define	CAN_USDT_DLC		8
/* Max size of USDT Single Frame data payload */
#define	CAN_USDT_SF_MAX_DATA_SIZE	0x07
/* Protocol Control Information (PCI) */
#define	CAN_USDT_PCI_FRAME_TYPE_MSK	0xf0
#define	CAN_USDT_PCI_DATA_MSK	0x0f
/* Single Frame */
#define	CAN_USDT_PCI_SF		0x00
/* First Frame */
#define	CAN_USDT_PCI_FF		0x10
/* Consecutive Frame */
#define	CAN_USDT_PCI_CF		0x20
/* Flow Control Frame */
#define	CAN_USDT_PCI_FC		0x30
/* TRUE if the frame is Single Frame */
#define	CAN_USDT_IS_SF(x)	\
    (((x)->data[0] & CAN_USDT_PCI_FRAME_TYPE_MSK) == CAN_USDT_PCI_SF)
/* TRUE if the frame is First Frame */
#define CAN_USDT_IS_FF(x)	\
    (((x)->data[0] & CAN_USDT_PCI_FRAME_TYPE_MSK) == CAN_USDT_PCI_FF)
/* TRUE if the frame is Consecutive Frame */
#define CAN_USDT_IS_CF(x)	\
    (((x)->data[0] & CAN_USDT_PCI_FRAME_TYPE_MSK) == CAN_USDT_PCI_CF)
/* Extracts data length from USDT frame */
#define	CAN_USDT_GET_DL(x)	\
    (CAN_USDT_IS_SF(x) ? (x)->data[0] & CAN_USDT_PCI_DATA_MSK : \
    ((x)->data[0] & CAN_USDT_PCI_DATA_MSK) + (x)->data[1])

/* State machine handling re-assemling OBD message from USDT frames. */
static enum {
	CAN_USDT_RX_ST_IDLE = 0,	/* idle */
	CAN_USDT_RX_ST_SF_FF,		/* waits for SF or FF */
	CAN_USDT_RX_ST_CF		/* receives CFs */
} can_usdt_rx_st;


/* Map symbolic supported speeds to real values. */
static const UINT32 can_speeds[CAN_NUM_SUPPORTED_SPEEDS] = {
    1000000,	/* 1 Mbit */
    500000,	/* 500 kbit */
    250000,	/* 250 kbit */
    125000	/* 125 kbit */
};

/* local macros */
/* get current mode */
#define CAN_GET_MODE		can_mode
/* set current mode */
#define CAN_SET_MODE(x)		(can_mode = (x))

/* local types */

/* local variables */
/* state machine handling packets received from CAN module */
static enum {
	CAN_RX_ST_IDLE = 0,	/* idle */
	CAN_RX_ST_RECEIVEPKT	/* receive frame */
} can_rx_st;

/* state machine handling packets transmitted by CAN module */
static enum {
	CAN_TX_ST_IDLE = 0,	/* idle */
	CAN_TX_ST_WAIT4TXDONE,	/* wait for transmission to be done */
	/* wait for transmission to be done, send result to master */
	CAN_TX_ST_WAIT4TXDONE_SENDRES,
	CAN_TX_ST_ABORT,	/* abort transmission */
	/* abort transmission, inform master */
	CAN_TX_ST_ABORT_SENDRES,
	CAN_TX_ST_FAULT,	/* driver could not be safely stopped */
	CAN_TX_ST_CONF1		/* apply configuration to the CAN module */
} can_tx_st;

/* Set to TRUE when CAN frame is received. Must be cleared by consumer. */
static BOOL can_frame_received;
/* Copy of received frame waiting for processing by OBD layer. */
static carmon_can_frame_t can_rx_frame;

/* communication with master */
/* structure holding ACK/NAK response to master */
static mcom_response_t can_resp_ack_nak;

/* structure holding data response to master */
static mcom_response_t can_resp_data;

/* response data buffer - must be large enough to hold the longest response. */
static char can_resp_buf[40];

/* Configuration parameters. */
/* Communication mode */
static int can_mode;
/* Communication speed */
static CAN_COMM_SPEED can_speed;
/* Filtering mode. */
static int can_filter_mode;
/* Acceptance filter. */
static UINT32 can_filter;
/* Filter mask. */
static UINT32 can_filter_mask;
/* If TRUE, sends ACK to master after done with configuration. */
static BOOL can_conf_send_ack;

/* OBD RX timeout timer */
static rtl_timer_t *can_obd_rx_to_timer;
/* CAN TX deadline, in microseconds; owned by the CAN/OBD task. */
static int64_t can_tx_deadline;

/* local functions */

/*
 * Sends data packet to master.
 */
int
can_send_data_response_to_master(char *hdr, char *buf)
{
	int err;

	err = mcom_prepare_generic_response(&can_resp_data, hdr,
	    buf, MCOM_DATA_TYPE_ASCII, strlen(buf), MCOM_DONT_FRAGMENT);

	if (err == 0) {
		mcom_queue_response(&can_resp_data);
	}

	return err;
}

/*
 * Sends ACK to master.
 */
static int
can_send_ack_response_to_master(void)
{
	int err;

	err = mcom_prepare_ack_response(&can_resp_ack_nak);

	if (err == 0) {
		mcom_queue_response(&can_resp_ack_nak);
	}

	return err;
}

/*
 * Sends NAK to master.
 */
static int
can_send_nak_response_to_master(unsigned char e)
{
	int err;

	err = mcom_prepare_nak_response(&can_resp_ack_nak, e);

	if (err == 0) {
		mcom_queue_response(&can_resp_ack_nak);
	}

	return err;
}

/*
 * Schedules CAN frame for transmission.
 * Returns:
 *       0  - packet successfully scheduled for transmission
 *   EBUSY  - module is currently busy transmitting previous packet
 *   EINVAL - invalid packet format
 */
static int
can_send_frame(char *pkt, int ln)
{
	carmon_can_frame_t frame = { 0 };
	carmon_can_frame_t *txm = &frame;
	int dlc, type, id, i;
	int err;

	if (can_tx_st != CAN_TX_ST_IDLE) {
		return (EBUSY);
	}

	/*
	 * Validate packet. Packet format:
	 *
	 * --------------------------
	 * | TYPE | ID | DLC | DATA |
	 * --------------------------
	 *
	 * where:
	 *
	 *  TYPE   message type (1 byte)
	 *          - bit 0 - RTR (1 for remote frame)
	 *          - bit 1 - EID (1 for extended ID)
	 *  ID     message identifier (4 bytes)
	 *          - TYPE[1] == 1: 29-bit extended
	 *          - TYPE[1] == 0: 11-bit standard
	 *  DLC    data length code - 0..8 (1 byte)
	 *  DATA   data payload (0..8 bytes)
	 */
	if (ln < 12)
		return EINVAL;

	dlc = (int) hex2ulong(pkt + 10, 2);

	if (dlc > 8 || ln != 12 + (2 * dlc)) {
		return (EINVAL);
	}

	type = (int) (hex2ulong(pkt, 2) & 0x03);
	id = (int) (hex2ulong(pkt + 2, 8) & 0x1fffffff);

	/* ID */
	txm->msgSID.SID = type & 0x02 ? id >> 18 : id & 0x7ff;
	txm->msgEID.EID = type & 0x02 ? id & 0x3ffff : 0;

	/* DLC */
	txm->msgEID.DLC = dlc;

	/* RTR - TODO */
	txm->msgEID.RTR = 0;

	/* IDE */
	txm->msgEID.IDE = type >> 1;

	/* Add data payload. */
	for (i = 0; i < dlc; i++) {
		txm->data[i] = hex2ulong(pkt + 12 + (2 * i), 2);
	}

	/* Schedule a copy of the prepared frame for transmission. */
	err = can_twai_transmit(txm);
	if (err != 0) {
		return (err == EBUSY || err == EINVAL ? err : EIO);
	}

	/* Arm TX timeout. */
	can_tx_deadline = esp_timer_get_time() + CAN_TX_TIMEOUT;

	/* Wait for transmission completion before responding to master. */
	can_tx_st = CAN_TX_ST_WAIT4TXDONE_SENDRES;

	return (0);
}

/*
 * Service processing received CAN frames.
 */
static int
can_rx_svc(void)
{
	carmon_can_frame_t rxm;
	int err;

	switch (can_rx_st) {
	/* service idle */
	case CAN_RX_ST_IDLE:
		break;

	/* Wait for received message. */
	case CAN_RX_ST_RECEIVEPKT:
		err = can_twai_receive(&rxm);
		if (err == EAGAIN) {
			break;
		}
		if (err != 0) {
			return (err);
		}

		/*
		 * An unrestricted hardware filter can accept both types.
		 * Preserve the requested SID/EID selection.
		 */
		if (CAN_GET_MODE != MODE_LISTEN_ALL_MESSAGES &&
		    rxm.msgEID.IDE != ((can_filter_mode >> 1) & 1)) {
			break;
		}

		/* Let the received frame undergo process of analysis. */
		can_snoop_process_frame(&rxm);

		/*
		 * If OBD layer is operating in KWP2000/CAN mixed mode,
		 * then this OBD callback takes care of processing
		 * non-OBD CAN frames.
		 */
		obd_can_rx_cb(&rxm);

		/*
		 * Pass received message to upper layer.
		 * Discard the frame if upper layer is not done with processing
		 * previously received frame.
		 */
		if (!can_frame_received) {
			/*
			 * Keep a separate copy valid after this function
			 * returns and while subsequent frames are received.
			 */
			can_rx_frame = rxm;

			can_frame_received = TRUE;
		}

		break;

	/* Should not happen. */
	default:
		break;
	} /* switch */

	return (0);
}

/*
 * Applies prepared configuration to the CAN module.
 *
 * Initializes
 *   - CAN mode
 *   - speed
 *   - acceptance filter.
 */
static int
can_conf_apply(void)
{
	int err;

	/* Discard the frame retained for the previous configuration. */
	can_frame_received = FALSE;

	err = can_twai_configure(CAN_GET_MODE, can_speeds[can_speed],
	    can_filter_mode, can_filter, can_filter_mask);
	if (err != 0) {
		return (err);
	}

	can_rx_st = CAN_GET_MODE == MODE_DISABLED ?
	    CAN_RX_ST_IDLE : CAN_RX_ST_RECEIVEPKT;

	return (0);
}

/*
 * Service transmitting CAN frames.
 */
static int
can_tx_svc(void)
{
	int err;
	BOOL send_response;

	switch (can_tx_st) {
	/* service idle */
	case CAN_TX_ST_IDLE:
		break;

	/* service faulted */
	case CAN_TX_ST_FAULT:
		return (EIO);

	/* Wait for packet to be transmitted. */
	case CAN_TX_ST_WAIT4TXDONE:
	case CAN_TX_ST_WAIT4TXDONE_SENDRES:
		err = can_twai_tx_result();

		if (err == EAGAIN) {
			if (esp_timer_get_time() < can_tx_deadline) {
				break;
			}

			/* Abort transmission, since timeout expired. */
			can_tx_st =
			    can_tx_st == CAN_TX_ST_WAIT4TXDONE_SENDRES ?
			    CAN_TX_ST_ABORT_SENDRES : CAN_TX_ST_ABORT;

			break;
		}

		/*
		 * Unexpected backend state: stop the driver before
		 * allowing its TX storage to be reused.
		 */
		if (err != 0 && err != EIO) {
			can_tx_st =
			    can_tx_st == CAN_TX_ST_WAIT4TXDONE_SENDRES ?
			    CAN_TX_ST_ABORT_SENDRES : CAN_TX_ST_ABORT;

			break;
		}

		/*
		 * If master initiated transmission, inform master about
		 * the result.
		 * Send NAK response containing error code in case of error;
		 * ACK otherwise.
		 */
		if (can_tx_st == CAN_TX_ST_WAIT4TXDONE_SENDRES) {
			if (err == 0) {
				can_send_ack_response_to_master();
			} else {
				can_send_nak_response_to_master(
				    MCOM_NAK_IO_ERROR);
			}
		}

		can_tx_st = CAN_TX_ST_IDLE;
		break;

	/* Abort transmission. */
	case CAN_TX_ST_ABORT:
	case CAN_TX_ST_ABORT_SENDRES:
		send_response = can_tx_st == CAN_TX_ST_ABORT_SENDRES;

		err = can_twai_abort();
		if (err == 0) {
			err = can_conf_apply();
		}

		/* Discard the frame retained for the previous transaction. */
		can_frame_received = FALSE;

		can_tx_st = err == 0 ?
		    CAN_TX_ST_IDLE : CAN_TX_ST_FAULT;

		if (send_response) {
			can_send_nak_response_to_master(MCOM_NAK_IO_ERROR);
		}

		if (err != 0) {
			return (err);
		}

		break;

	/* Switch to configuration mode. */
	case CAN_TX_ST_CONF1:
		err = can_conf_apply();

		can_tx_st = err == 0 ?
		    CAN_TX_ST_IDLE : CAN_TX_ST_FAULT;

		if (can_conf_send_ack) {
			can_conf_send_ack = FALSE;

			if (err == 0) {
				can_send_ack_response_to_master();
			} else {
				can_send_nak_response_to_master(
				    MCOM_NAK_IO_ERROR);
			}
		}

		return (err);

	default:
		break;
	} /* switch */

	return 0;
}

/*
 * Main service routine - called periodically from CAN task.
 */
int
can_svc(void)
{
	int err;

	/* Snoop callback function. Handles snooping timers. */
	can_snoop_cb();

	/* Handle packets to be transmitted by CAN module. */
	err = can_tx_svc();
	if (err != 0) {
		return (err);
	}

	/* Handle packets received by CAN module. */
	return (can_rx_svc());
}

/*
 * Initialize CAN module.
 */
static int
can_hw_init(void)
{
	return (can_conf_apply());
}

/*
 * Prepares process of CAN module configuration according to data
 * received from master.
 *
 * Mode #1: communication mode and speed
 *
 *  ---------------------
 *  | 01 | MODE | SPEED |
 *  ---------------------
 *
 * where:
 *
 * MODE  communication mode (1 byte)
 *        - bit 7 - if set, received CAN frames are directly sent to the master
 *        - bit 6..0 - CAN mode
 *          0 - normal
 *          1 - disabled
 *          2 - loopback
 *          3 - listen only
 *          4 - listen all messages
 * SPEED communication speed (1 byte)
 *        0 - 1Mbit
 *        1 - 500kbit
 *        2 - 250kbit
 *        3 - 125kbit
 *
 * Mode #2: acceptance filter and mask
 *
 *  -----------------------------
 *  | 02 | MODE | FILTER | MASK |
 *  -----------------------------
 *
 * where:
 *
 * MODE   filtering mode
 *         - bit 0 - if set, message type (specified via bit 1) is compared;
 *                   if cleared, bit 1 is ignored and both SID and EID messages
 *                   can be received.
 *         - bit 1 - if cleared, SID messages are received;
 *                   if set, EID messages are received.
 * FILTER SID/EID to be accepted
 * MASK   specifies which ID bits should be compared
 *        (0 - particular bit is not compared) 
 */
static int
can_configure_decode(char *data, int ln)
{
	/* Only "01" and "02" configuration modes supported for now. */
	if (data[0] != '0' || (data[1] != '1' && data[1] != '2')) {
		return EINVAL;
	}

	/* Mode #1 */
	if (data[1] == '1') {
		int mode, speed;

		/* Validate packet length */
		if (ln != 6) {
			return EINVAL;
		}

		/* Obtain communication mode. */
		mode = (int) hex2ulong(data + 2, 2);

		/* Obtain speed. */
		speed = (int) hex2ulong(data + 4, 2);

		/* Let the service finish process of configuration. */
		return can_configure(mode, speed, -1, 0xffffffff,
		    0xffffffff, TRUE);
	} else {
		/* Mode #2 */
		int mode;
		UINT32 filter, mask;

		/* Validate packet length */
		if (ln != 20) {
			return EINVAL;
		}

		/* Obtain filtering mode. */
		mode = (int) hex2ulong(data + 2, 2);

		/* Obtain filter. */
		filter = (int) hex2ulong(data + 4, 8);
		/* Only 29 bits are relevant. */
		filter &= 0x1fffffff;

		/* Obtain mask. */
		mask = (int) hex2ulong(data + 12, 8);
		/* Only 29 bits are relevant. */
		mask &= 0x1fffffff;

		/* Let the service finish process of configuration. */
		return can_configure(-1, -1, mode, filter, mask, TRUE);
	}
}

/* global functions */
/*
 * Checks if configuration process is in progress.
 * Returns:
 *   TRUE if configuration is in progress; FALSE otherwise.
 */
BOOL
can_configuration_in_progress(void) {
	return (can_tx_st == CAN_TX_ST_CONF1);
}

/*
 * Configures the CAN module.
 * Returns:
 *   0      - success.
 *   EINVAL - specified mode or speed not supported.
 *   EBUSY  - module currently busy, speed can't be set.
 */
int
can_configure(int mode, int speed, int filter_mode, UINT32 filter,
    UINT32 mask, BOOL send_ack)
{
	/*
	 * Transmit service is used to apply configuration, so check if that
	 * service is currently available.
	 */
	if (can_tx_st != CAN_TX_ST_IDLE) {
		return EBUSY;
	}

	if (mode < -1 || mode >= CAN_NUM_SUPPORTED_MODES ||
	    speed < -1 || speed >= CAN_NUM_SUPPORTED_SPEEDS ||
	    filter_mode < -1 || filter_mode > 3) {
		return (EINVAL);
	}

	if ((mode == -1 ? CAN_GET_MODE : mode) != MODE_DISABLED &&
	    (mode == -1 ? CAN_GET_MODE : mode) !=
	    MODE_LISTEN_ALL_MESSAGES &&
	    !((filter_mode == -1 ? can_filter_mode : filter_mode) & 1)) {
		return (EINVAL);
	}

	/* Communication mode is to be configured. */
	if (mode != -1) {
		if (mode > MODE_LISTEN_ALL_MESSAGES) {
			return EINVAL;
		}

		CAN_SET_MODE(mode);
	}

	/* Speed is to be configured. */
	if (speed != -1) {
		if (speed >= CAN_NUM_SUPPORTED_SPEEDS) {
			return EINVAL;
		}

		can_speed = speed;
	}

	/* Filtering mode is to be configured. */
	if (filter_mode != -1) {
		if (filter_mode > 0x03) {
			return EINVAL;
		}

		can_filter_mode = filter_mode;
	}

	/* Acceptance filter is to be configured. */
	if (filter != 0xffffffff) {
		can_filter = filter;
	}

	/* Filter mask is to be configured. */
	if (mask != 0xffffffff) {
		can_filter_mask = mask;
	}

	/* If TRUE, sends ACK to master once configuration is finished. */
	can_conf_send_ack = send_ack;

	can_tx_st = CAN_TX_ST_CONF1;

	return 0;
}

/*
 * Initialization routine.
 */
int
can_init(void)
{
	int err;

	/* Default is "Listen Only" communication mode. */
	CAN_SET_MODE(MODE_LISTEN_ONLY);

	/* Default speed is 500kb. */
	can_speed = SPEED_500k;

	/*
	 * Configure mask 0 and filter 0 to accept SID diagnostic messages
	 * only. Tester sends frames with diagnostic functional address
	 * 0x7DF. ECUs also accept 0x7E0 - 0x7E7 range of physical
	 * addresses and respond with physical addresses which are within
	 * range of 0x7E8 - 0x7EF.
	 */
	can_filter_mode = 0x01;
	can_filter_mask = 0x7c0;
	can_filter = 0x7df;

	/* Initialize state machine for receiving CAN frames. */
	can_rx_st = CAN_RX_ST_RECEIVEPKT;
	can_frame_received = FALSE;

	/* Initialize state machine for transmitting CAN frames. */
	can_tx_st = CAN_TX_ST_IDLE;

	/* Initialize state machine for receiveing USDT frames. */
	can_usdt_rx_st = CAN_USDT_RX_ST_IDLE;

	/* Set module ID for purposes of communication with master. */
	mcom_response_init(&can_resp_ack_nak, MCOMM_CAN_ID);
	mcom_response_init(&can_resp_data, MCOMM_CAN_ID);

	/* Initialize CAN analyzer. */
	can_snoop_init();

	/* Initialize CAN OBD RX timer. */
	can_obd_rx_to_timer = NULL;
	err = rtl_timer_init(&can_obd_rx_to_timer, 0);
	if (err != 0) {
		return (err);
	}

#if 0
	/* Initialize CAN TX timeout timer. */
	can_tx_to_timer = NULL;
	rtl_timer_init(&can_tx_to_timer, 0);

	/* Register and enable RFID service. */
	rtl_svc_init("Can", can_svc, TRUE, FALSE);
#endif /* 0 */

	/*
	 * Initialize CAN module.
	 */
	err = can_hw_init();
	if (err != 0) {
		can_tx_st = CAN_TX_ST_FAULT;
	}

	return (err);
}

/*
 * Suspend the module.
 * Called only from the CAN/OBD task after stopping OBD processing.
 */
int
can_suspend(void)
{
	int err;

	/*
	 * Let a pending transmission or configuration finish first.
	 * FAULT permits another attempt to release the controller.
	 */
	if (can_tx_st != CAN_TX_ST_IDLE &&
	    can_tx_st != CAN_TX_ST_FAULT) {
		return (EBUSY);
	}

	err = can_twai_abort();

	can_rx_st = CAN_RX_ST_IDLE;
	can_frame_received = FALSE;
	can_usdt_rx_st = CAN_USDT_RX_ST_IDLE;

	if (err != 0) {
		can_tx_st = CAN_TX_ST_FAULT;
		return (err);
	}

	CAN_SET_MODE(MODE_DISABLED);
	can_conf_send_ack = FALSE;
	can_tx_st = CAN_TX_ST_IDLE;

	return (0);
}

/*
 * Returns TRUE if current data response is in use; FALSE otherwise.
 */
BOOL
can_response_inuse(void)
{
	return mcom_response_inuse(&can_resp_data);
}

/*
 * Processes query from master targeted to CAN module
 */
int
can_process_query(char *data, int ln)
{
	int err = 0;
	BOOL translate_err = TRUE;

	/* Assume ACK/NAK will be sent to master */
	BOOL fl_send_ack_nak = TRUE;

	/* Handle particular commands */
	switch (data[0]) {
		/* Configure the module. */
		case MCOMM_CAN_CMD_CFG:
			/* configure module */
			err = can_configure_decode(data + 1, ln - 1);

			/*
			 * If configuration process has been successfully
			 * commenced, ack/nak will be sent later -
			 * once the process of configuration is finished.
			 */
			if (err == 0) {
				fl_send_ack_nak = FALSE;
			}

			break;

		/* Analyze the bus. */
		case MCOMM_CAN_CMD_SNOOP:
			err = can_snoop_process_query(data + 1, ln - 1);

			break;

		/* Send CAN frame. */
		case MCOMM_CAN_CMD_SEND_FRAME:
			/* Schedule packet for transmission. */
			err = can_send_frame(data + 1, ln - 1);

			/*
			 * If packet has been successfully scheduled
			 * for transmission, ack/nak will be sent later -
			 * once the process of transmission is finished.
			 */
			if (err == 0) {
				fl_send_ack_nak = FALSE;
			}

			break;

		/* Get status of the module. */
		case MCOMM_CAN_CMD_GET_STATUS: {
			can_twai_status_t status;

			/* Communication channel busy. */
			if (mcom_response_inuse(&can_resp_data)) {
				err = MCOM_NAK_MODULE_BUSY;
				translate_err = FALSE;
				break;
			}

			err = can_twai_get_status(&status);

			/*
			 * Disabled mode deliberately has no TWAI node.
			 * Return an empty status for this known condition.
			 */
			if (err == ENODEV &&
			    CAN_GET_MODE == MODE_DISABLED &&
			    can_tx_st == CAN_TX_ST_IDLE) {
				memset(&status, 0, sizeof (status));
				err = 0;
			}

			if (err != 0) {
				break;
			}

			snprintf(can_resp_buf, sizeof (can_resp_buf),
			    "%02X%02X%02X%02X",
			    (unsigned int)status.flags,
			    (unsigned int)status.tx_errors,
			    (unsigned int)status.rx_errors,
			    (unsigned int)(
			    can_snoop_get_num_dropped_frames() & 0xff));

			/* Send the message to the master. */
			err = can_send_data_response_to_master("00",
			    can_resp_buf);
			if (err == 0) {
				fl_send_ack_nak = FALSE;
			}

			break;
		}

		/* unknown command */
		default:
			err = MCOM_NAK_UNKNOWN_IDCMD;
			translate_err = FALSE;

			break;
	}

	/* ACK/NAK will be sent later */
	if (!fl_send_ack_nak)
		return 0;

	/*
	 * Send response to master indicating that command was processed.
	 * If no error occurred, respond with ACK; otherwise respond with NAK
	 * and error code.
	 */
	if (err == 0) {
		can_send_ack_response_to_master();
	} else {
		/* Translate error code if appropriate. */
		if (translate_err)
			err = mcom_err_translate(err);

		can_send_nak_response_to_master(err);
	}

	return 0;
}

/*
 * Sends USDT frame to the bus.
 *
 * Returns:
 *        0 - packet scheduled for transmission
 *   EBUSY  - currently busy with sending previous packets
 *   EINVAL - unsupported type of frame
 */
int
can_usdt_send_frame(int frame_type, unsigned char *data, int ln)
{
	carmon_can_frame_t frame = { 0 };
	carmon_can_frame_t *txm = &frame;
	int i, err;

	/* Validate frame type. */
	if ((frame_type != CAN_USDT_PCI_SF) &&
	    (frame_type != CAN_USDT_PCI_FC)) {
		return EINVAL;
	}

	if (frame_type == CAN_USDT_PCI_SF && (data == NULL || ln <= 0)) {
		return (EINVAL);
	}

	if (can_tx_st != CAN_TX_ST_IDLE) {
		return (EBUSY);
	}

	/*
	 * ID - only SID for now
	 * TODO: add support for EID
	 */
	txm->msgSID.SID = CAN_OBD_TESTER_SID;
	txm->msgEID.EID = 0;

	/* DLC - always 8 bytes are sent per standard */
	txm->msgEID.DLC = CAN_USDT_DLC;

	/* RTR */
	txm->msgEID.RTR = 0;

	/*
	 * IDE - only SID for now
	 * TODO: add support for EID
	 */
	txm->msgEID.IDE = 0;

	/* Fill in data payload according to frame type. */
	if (frame_type == CAN_USDT_PCI_SF) {
	/* Single Frame */
		/* Make sure we don't write past buffer. */
		if (ln > CAN_USDT_SF_MAX_DATA_SIZE) {
			ln = CAN_USDT_SF_MAX_DATA_SIZE;
		}

		/* Fill in PCI byte - frame type and size of data payload. */
		txm->data[0] = CAN_USDT_PCI_SF | ln;

		for (i = 0; i < ln; i++) {
			txm->data[i + 1] = data[i];
		}
	} else if (frame_type == CAN_USDT_PCI_FC) {
	/* Flow Control Frame */
		/* Fill in PCI byte. */
		txm->data[0] = CAN_USDT_PCI_FC;
	}

	/* Schedule a copy of the prepared frame for transmission. */
	err = can_twai_transmit(txm);
	if (err != 0) {
		return (err == EBUSY || err == EINVAL ? err : EIO);
	}

	/* Arm TX timeout. */
	can_tx_deadline = esp_timer_get_time() + CAN_TX_TIMEOUT;

	/* Wait for transmission completion. */
	can_tx_st = CAN_TX_ST_WAIT4TXDONE;

	return (0);
}

/*
 * Encodes OBD packet to USDT Single Frame and sends the resulting packet
 * to the bus.
 *
 * Returns:
 *        0 - packet scheduled for transmission
 *   EBUSY  - currently busy with sending previous packets
 *   EINVAL - unsupported type of frame
 */
int
can_obd_send(unsigned char *data, int ln)
{
	int ret = can_usdt_send_frame(CAN_USDT_PCI_SF, data, ln);

	/* Packet sent out successfully, wait for response. */
	if (ret == 0) {
		/* Arm Rx timer */
		rtl_timer_init(&can_obd_rx_to_timer, CAN_OBD_RX_TIMEOUT);

		can_usdt_rx_st = CAN_USDT_RX_ST_SF_FF;
	}

	return ret;
}

/*
 * Receives and re-assembles OBD packet.
 * Implements "Unacknowledged Segmented Data Transfer" (USDT)
 * mechanism used to support multiple consecutive frame responses
 * to a single frame request.
 *
 * The following unique CAN message types are supported by the network
 * USDT protocol layer:
 *
 *  - Single Frame (SF) - supports a maximum length of seven (7) data bytes
 *  - First Frame (FF)  - the first frame CAN message of a diagnostic
 *    communication sequence
 *  - Consecutive Frame (CF) - the second to n frame CAN message
 *    of a diagnostic communication sequence
 *  - Flow Control (FC) - the receiving node diagnostic communication
 *    handshake used to adjust the sender node to the network layer
 *    capabilities of the receiver
 *
 * Returns:
 *        0 - packet received
 *   EAGAIN - operation in progress
 *   EIO    - ECU not responded or malformed packet received
 */
int
can_obd_recv(unsigned char **data, int *ln)
{
	static unsigned char usdt_data[CAN_USDT_MAX_DATA_SIZE];
	static int rln, tail;

	carmon_can_frame_t *rxm;
	int id, ft;
	int dln, i;

	/* If ECU has not responded in given time, abort. */
	if (rtl_timer_expired(can_obd_rx_to_timer)) {
		/* Reset re-assembly state machine. */
		can_usdt_rx_st = CAN_USDT_RX_ST_IDLE;

		return EIO;
	}

	/* Packet not received yet. */
	if (!can_frame_received) {
		return EAGAIN;
	}

	/* Save pointer to received frame and clear the flag. */
	rxm = &can_rx_frame;
	can_frame_received = FALSE;

	/*
	 * Acceptance mask allows packets within ID range 0x7c0 - 0x7ff.
	 * Thus, filter out packets not coming from ECU.
	 */
	id = CAN_GET_ID(rxm);
	if ((id & CAN_OBD_ECU_SID_MSK) != CAN_OBD_ECU_SID) {
		return EAGAIN;
	}

	/* Verify size of data payload - must be always 8 per standard. */
	if (rxm->msgEID.DLC != CAN_USDT_DLC) {
		return EIO;
	}

	/* Decode frame type. */
	ft = rxm->data[0] & CAN_USDT_PCI_FRAME_TYPE_MSK;

	/* USDT re-assembly state machine */
	switch (can_usdt_rx_st) {
	/* idle */
	case CAN_USDT_RX_ST_IDLE:
		break;

	/* Waits for SF or FF. */
	case CAN_USDT_RX_ST_SF_FF:
		/* Validate packet type */
		if ((ft != CAN_USDT_PCI_SF) && (ft != CAN_USDT_PCI_FF)) {
			can_usdt_rx_st = CAN_USDT_RX_ST_IDLE;

			return EIO;
		}

		/* Extract size of data payload. */
		dln = CAN_USDT_GET_DL(rxm);

		/* Single Frame */
		if (ft == CAN_USDT_PCI_SF) {
			/* Invalid length of data payload. */
			if ((dln > CAN_USDT_SF_MAX_DATA_SIZE) || (dln == 0)) {
				can_usdt_rx_st = CAN_USDT_RX_ST_IDLE;

				return EIO;
			}

			/* Extract data payload. Skip PCI byte. */
			for (i = 0; i < dln; i++) {
				usdt_data[i] = rxm->data[i + 1];
			}

			/*
			 * We are done, pass pointer to local storage
			 * and its size to the caller.
			 */
			*data = usdt_data;
			*ln = dln;

			can_usdt_rx_st = CAN_USDT_RX_ST_IDLE;

			return 0;
		}

		/* First Frame */
		if (ft == CAN_USDT_PCI_FF) {
			/*
			 * Invalid length of data payload.
			 * 7 and less data bytes should be always delivered
			 * in form of Single Frames.
			 */
			if ((dln > CAN_USDT_MAX_DATA_SIZE) || (dln < 8)) {
				can_usdt_rx_st = CAN_USDT_RX_ST_IDLE;

				return EIO;
			}

			/*
			 * Extract data payload. Skip PCI and DL bytes.
			 * First Frame always carries 6 bytes of data payload.
			 */
			for (i = 0; i < 6; i++) {
				usdt_data[i] = rxm->data[i + 2];
			}

			/*
			 * Save pointer to free position in buffer
			 * and remaining number of bytes to be received.
			 */
			rln = dln - 6;
			tail = 6;

			/*
			 * According to the standard, Flow Control Frame
			 * has to be sent to the sender within 5ms
			 * after FF is received to indicate we are ready
			 * to receive CFs.
			 * FS, BSmax and STmin have to be zero for OBD
			 * communication, so no data to be passed down.
			 * Abort if FC can't be sent.
			 */
			if (can_usdt_send_frame(CAN_USDT_PCI_FC, NULL, 0)
			    != 0) {
				can_usdt_rx_st = CAN_USDT_RX_ST_IDLE;

				return EIO;
			}

			/* Wait for subsequent CFs. */
			can_usdt_rx_st = CAN_USDT_RX_ST_CF;

			return EAGAIN;
		}

		break;

	/* Receives CFs */
	case CAN_USDT_RX_ST_CF:
		/* Validate packet type */
		if (ft != CAN_USDT_PCI_CF) {
			can_usdt_rx_st = CAN_USDT_RX_ST_IDLE;

			return EIO;
		}

		/* TODO: Validate Sequence Number */

		/* All CFs except of the last one carry full data payload. */
		dln = rln >= CAN_USDT_SF_MAX_DATA_SIZE ?
		    CAN_USDT_SF_MAX_DATA_SIZE : rln;

		/*
		 * Extract data payload. Skip PCI byte.
		 */
		for (i = 0; i < dln; i++) {
			usdt_data[tail + i] = rxm->data[i + 1];
		}

		/* Update "number of remaining bytes" and buffer pointer. */
		rln -= dln;
		tail += dln;

		/* There are still data to be received. */
		if (rln > 0) {
			return EAGAIN;
		} else {
			/*
			 * We are done, pass pointer to local storage
			 * and its size to the caller.
			 */
			*data = usdt_data;
			*ln = tail;

			/* Switch to IDLE mode. */
			can_usdt_rx_st = CAN_USDT_RX_ST_IDLE;

			return 0;
		}

		break;

	/* Not reached */
	default:
		break;
	}

	/* Report success by default. */
	return 0;
}
