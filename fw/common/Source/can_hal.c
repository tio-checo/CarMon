/*********************************************************************
 *
 * Support for Controller Area Network (CAN) PIC module - provides
 * API for master device.
 *
 *********************************************************************/

#include <errno.h>
#include <stdio.h>

#include "HardwareProfile.h"
#include "GenericTypeDefs.h"
#include "xc.h"
#include "CAN.h"
#include "CANTypes.h"

#include "master_comm.h"
#include "can_hal.h"
#include "rt_light.h"
#include "utils.h"

/* constants */
/* channel 0 is dedicated to receive buffer */
#define CAN_RX_CHANNEL	CAN_CHANNEL0
/* size of receive buffer */
#define CAN_RX_CHANNEL_SIZE	32
/* channel 1 is dedicated to transmit buffer */
#define CAN_TX_CHANNEL	CAN_CHANNEL1
/* size of transmit buffer */
#define CAN_TX_CHANNEL_SIZE	1

/*
 * Bit timing configurations for supported speeds. Values specified
 *  - number of Time Quantum for Phase Segment 2 (PS2) - 1..8
 *  - number of Time Quantum for Phase Segment 1 (PS1) - 1..8
 *  - number of Time Quantum for Propagation Segment (PS) - 1..8
 *  - if PS2 specified by code (TRUE) or set automatically (FALSE)
 *  - if bit is sampled three times (TRUE) or once (FALSE)
 *  - number of Time Quantum for synchronization jump (SFW) - 1..4
 */
static const CAN_BIT_CONFIG can_bit_timing_cfg[CAN_NUM_SUPPORTED_SPEEDS] = {
    /* PS2, PS1, PS, PS2 set in code, bit sampled three times, SFW */
    /* 1 Mbit */
    {CAN_BIT_4TQ, CAN_BIT_5TQ, CAN_BIT_5TQ, TRUE, TRUE, CAN_BIT_3TQ},
    /* 500 kbit */
    {CAN_BIT_4TQ, CAN_BIT_5TQ, CAN_BIT_5TQ, TRUE, TRUE, CAN_BIT_3TQ},
    /* 250 kbit */
    {CAN_BIT_4TQ, CAN_BIT_5TQ, CAN_BIT_5TQ, TRUE, TRUE, CAN_BIT_3TQ},
    /* 125 kbit */
    {CAN_BIT_4TQ, CAN_BIT_5TQ, CAN_BIT_5TQ, TRUE, TRUE, CAN_BIT_3TQ}
};

/* Map symbolic supported speeds to real values. */
static const UINT32 can_speeds[CAN_NUM_SUPPORTED_SPEEDS] = {
    1000000,	/* 1 Mbit */
    500000,	/* 500 kbit */
    250000,	/* 250 kbit */
    125000	/* 125 kbit */
};

/* local macros */
/* evaluates to TRUE if snoop mode is active */
#define CAN_SNOOP_ENABLED	(can_mode & 0x80)
/* enable snoop mode */
#define CAN_SNOOP_ENABLE	can_mode |= 0x80
/* disable snoop mode */
#define CAN_SNOOP_DISABLE	can_mode &= 0x7f
/* get current mode - filter out snoop bit */
#define CAN_GET_MODE		(can_mode & 0x7f)
/* set current mode */
#define CAN_SET_MODE(x)		(can_mode = (x))

/* local types */

/* local variables */
/* state machine handling packets received from CAN module */
static enum {
	CAN_RX_ST_IDLE = 0,	/* idle */
	CAN_RX_ST_WAIT4PKT	/* wait for received message */
} can_rx_st;

/* state machine handling packets transmitted by CAN module */
static enum {
	CAN_TX_ST_IDLE = 0,	/* idle */
	CAN_TX_ST_WAIT4TXDONE,	/* wait transmission to be done */
	CAN_TX_ST_CONF1,	/* configure CAN module */
	CAN_TX_ST_CONF2
} can_tx_st;

/*
 * CAN FIFOs
 *  FIFO0 - Receive - 32 message buffers
 *  FIFO1 - Transmit - 1 message buffer
 */
static unsigned int can_fifos[(CAN_RX_CHANNEL_SIZE + CAN_TX_CHANNEL_SIZE) * 4];

/* communication with master */
/* structure holding ACK/NAK response to master */
static mcom_response_t can_resp_ack_nak;
/* If set to TRUE, send ack/nak response packet to master */
static BOOL fl_send_ack_nak;

/* response data buffer - must be large enough to hold the longest response. */
static char can_resp_buf[40];
/* structure holding data response to master */
static mcom_response_t can_resp_data;
/* If set to TRUE, send response containing data to master */
static BOOL fl_send_data;

/* Communication mode */
static int can_mode;
/* Communication speed */
static CAN_COMM_SPEED can_speed;
/* If TRUE, sends ACK to master after done with configuration. */
static BOOL can_conf_send_ack;

/* local functions */

/*
 * Sends data packet to master.
 */
static int
can_send_data_response_to_master(char *pkt_hdr, char *buf, int ln)
{
	mcom_prepare_generic_response(&can_resp_data, pkt_hdr,
	    buf, MCOM_DATA_TYPE_ASCII, ln);

	fl_send_data = TRUE;

	return 0;
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
	CANTxMessageBuffer *txm;
	int dlc, type, id, i;

	if ((can_tx_st != CAN_TX_ST_IDLE) ||
	    ((txm = CANGetTxMessageBuffer(CAN1, CAN_TX_CHANNEL)) == NULL)) {
		return EBUSY;
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

	if (ln != 12 + (2 * dlc))
		return EINVAL;

	type = (int) (hex2ulong(pkt, 2) & 0x03);
	id = (int) (hex2ulong(pkt + 2, 8) & 0x1fffffff);

	/* Fill in message buffer with packet to be transmitted. */
	/* First clean up the buffer. */
	for (i = 0; i < 4; i++)
		txm->messageWord[i] = 0;

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

	/* Move pointer to the next free message buffer. */
	CANUpdateChannel(CAN1, CAN_TX_CHANNEL);

	/* Initiate the transmission. */
	can_tx_st = CAN_TX_ST_WAIT4TXDONE;
	CANFlushTxChannel(CAN1, CAN_TX_CHANNEL);

	return 0;
}

/*
 * Service processing received CAN frames.
 */
static int
can_rx_svc(void)
{
	CANRxMessageBuffer *rxm;

	switch (can_rx_st) {
	/* service idle */
	case CAN_RX_ST_IDLE:
		break;

	/* Wait for received message. */
	case CAN_RX_ST_WAIT4PKT:
		if ((rxm = CANGetRxMessage(CAN1, CAN_RX_CHANNEL)) == NULL)
			break;

		/* Send the message directly to master. */
		if (CAN_SNOOP_ENABLED) {
			int i;

			/*
			 * If module now busy with sending another response,
			 * try again later.
			 */
			if (fl_send_data) {
				break;
			}

			/*
			 * Convert received CAN frame to the response message.
			 * Format of the response data payload:
			 *
			 *  -----------------------------------
			 *  | TSTAMP | TYPE | ID | DLC | DATA |
			 *  -----------------------------------
			 *
			 * where:
			 *
			 *  TSTAMP timestamp (2 bytes)
			 *  TYPE   message type (1 byte)
			 *          - bit 0 - RTR (1 for remote frame)
			 *          - bit 1 - EID (1 for extended ID)
			 *  ID     message identifier (4 bytes)
			 *          - TYPE[1] == 1: 29-bit extended
			 *          - TYPE[1] == 0: 11-bit standard
			 *  DLC    data length code - 0..8 (1 byte)
			 *  DATA   data payload (0..8 bytes)
			 * 
			 */
			snprintf(can_resp_buf, sizeof (can_resp_buf),
			    "%04X%02X%08X%02X",
			    rxm->msgSID.CMSGTS,
			    rxm->msgEID.RTR | (rxm->msgEID.IDE << 1),
			    rxm->msgEID.IDE ? (rxm->msgSID.SID << 18) |
			    rxm->msgEID.EID : rxm->msgSID.SID,
			    rxm->msgEID.DLC);

			/* Add data payload. */
			for (i = 0; i < rxm->msgEID.DLC; i++) {
				int ln = strlen(can_resp_buf);
				char *rp = can_resp_buf + ln;

				ln = sizeof (can_resp_buf) - ln;

				/* Check we are not writing past the buffer. */
				if (ln <= 0) {
					break;
				}

				snprintf(rp, ln, "%02X", rxm->data[i]);
			}

			/* Send the message to the master. */
			can_send_data_response_to_master("R00", can_resp_buf,
			    strlen(can_resp_buf));
		}

		/* Update the message buffer pointer. */
		CANUpdateChannel(CAN1, CAN_RX_CHANNEL);

		break;

	/* Should not happen. */
	default:
		break;
	} /* switch */

	return 0;
}

/*
 * Service transmitting CAN frames.
 */
static int
can_tx_svc(void)
{
	CAN_TX_CHANNEL_CONDITION txc;

	switch (can_tx_st) {
	/* service idle */
	case CAN_TX_ST_IDLE:
		break;

	/* Wait for packet to be transmitted. */
	case CAN_TX_ST_WAIT4TXDONE:
		txc = CANGetTxChannelCondition(CAN1, CAN_TX_CHANNEL);

		/* Wait until all messages are transmitted. */
		if (txc & CAN_TX_CHANNEL_TRANSMITTING)
			break;

		/*
		 * If error occurred during transmission, inform master via
		 * NAK response containing error code; otherwise respond
		 * with ACK.
		 */
		txc &= CAN_TX_CHANNEL_ERROR | CAN_TX_CHANNEL_ARBITRATION_LOST;
		if (txc == 0) {
			mcom_prepare_ack_response(&can_resp_ack_nak);
		} else {
			mcom_prepare_nak_response(&can_resp_ack_nak,
			    0x80 | txc);
		}

		fl_send_ack_nak = TRUE;

		can_tx_st = CAN_TX_ST_IDLE;

		break;

	/* Configure communication speed. */
	case CAN_TX_ST_CONF1:
		/* Wait until module enters configuration mode. */
		if (CANGetOperatingMode(CAN1) != CAN_CONFIGURATION)
			break;

		/* Set speed. */
		CANSetSpeed(CAN1, &can_bit_timing_cfg[can_speed],
		    GetSystemClock(), can_speeds[can_speed]);

		/* Convert "Listen All Message" mode, preserve snoop bit. */
		if (CAN_GET_MODE == MODE_LISTEN_ALL_MESSAGES) {
			BOOL snoop_enabled = CAN_SNOOP_ENABLED;

			CAN_SET_MODE(CAN_LISTEN_ALL_MESSAGES);

			if (snoop_enabled) {
				CAN_SNOOP_ENABLE;
			}
		}

		/* Switch module to the desired operating mode. */
		CANSetOperatingMode(CAN1, CAN_GET_MODE);

		can_tx_st = CAN_TX_ST_CONF2;
		break;

	/* Finish configuration and send ACK to master. */
	case CAN_TX_ST_CONF2:
		/* Wait until module enters desired mode. */
		if (CANGetOperatingMode(CAN1) != CAN_GET_MODE)
			break;

		/* Send ACK to master indicating that configuration is done. */
		if (can_conf_send_ack) {
			can_conf_send_ack = FALSE;

			mcom_prepare_ack_response(&can_resp_ack_nak);
			fl_send_ack_nak = TRUE;
		}

		can_tx_st = CAN_TX_ST_IDLE;
		break;

	default:
		break;
	} /* switch */

	return 0;
}

/*
 * Takes care of sending response to master. Two responses might be scheduled
 * for transmission - ack/nak one and data one.
 * ack/nak response takes precedence over data response, since it is short,
 * thus it will occupy transmitter for shorter time.
 */
static int
can_comm_svc(void)
{
	/*
	 * There is ack/nak response to be sent to master, attempt to queue it
	 * for transmission.
	 */
	if (fl_send_ack_nak) {
		if (mcom_queue_response(&can_resp_ack_nak) != 0) {
			return EAGAIN;
		}

		fl_send_ack_nak = FALSE;

		/*
		 * Since transmitter is now busy, there is no point to continue
		 * with attempt to send data response. thus return.
		 */
		return 0;
	}

	/*
	 * There is data response to be sent to master, attempt to queue it
	 * for transmission.
	 */
	if (fl_send_data) {
		if (mcom_queue_response(&can_resp_data) != 0) {
			return EAGAIN;
		}

		fl_send_data = FALSE;
	}

	return 0;
}

/*
 * Main service routine - called periodically from timer interrupt.
 */
static int
can_svc(void)
{
	/* Handle packets received by CAN module. */
	can_rx_svc();

	/* Handle packets to be transmitted by CAN module. */
	can_tx_svc();

	/* Handle communication with master device. */
	can_comm_svc();

	return 0;
}

/*
 * Initializes CAN1 module.
 */
static int
can_hw_init(void)
{
	/* Turn CAN module ON. */
	CANEnableModule(CAN1, TRUE);

	/*
	 * Request CAN to enter "Configuration" mode
	 * and wait until the switch is done.
	 */
	CANSetOperatingMode(CAN1, CAN_CONFIGURATION);
	while(CANGetOperatingMode(CAN1) != CAN_CONFIGURATION);

	/*
	 * Initialize C1FIFOBA register with physical address
	 * of CAN message Buffer.
	 */
	CANAssignMemoryBuffer(CAN1, can_fifos, sizeof (can_fifos));

	/* Configure receive buffer */
	CANConfigureChannelForRx(CAN1, CAN_RX_CHANNEL, CAN_RX_CHANNEL_SIZE,
	    CAN_RX_FULL_RECEIVE);

	/* Configure transmit buffer */
	CANConfigureChannelForTx(CAN1, CAN_TX_CHANNEL, CAN_TX_CHANNEL_SIZE,
	    CAN_TX_RTR_DISABLED, CAN_LOWEST_PRIORITY);

	/*
	 * CANTMR value is stored on valid message reception
	 * and is stored with the message.
	 */
	C1CONbits.CANCAP = 1;

	/* Timestamp timer is incremented every 1ms. */
	CANSetTimeStampPrescalar(CAN1, (GetSystemClock() / 1000) - 1);

	/* 250kbit speed. */
	CANSetSpeed(CAN1, &can_bit_timing_cfg[can_speed], GetSystemClock(),
	    can_speeds[can_speed]);

	/*
	 * Configure mask 0 and filter 0 to accept all messages,
	 * since we want CAN module to operate in "promiscuous" mode.
	 */
	CANConfigureFilterMask(CAN1, CAN_FILTER_MASK0, 0, CAN_SID,
	    CAN_FILTER_MASK_ANY_TYPE);
	CANConfigureFilter(CAN1, CAN_FILTER0, 0, CAN_SID);
	CANLinkFilterToChannel(CAN1, CAN_FILTER0, CAN_FILTER_MASK0,
	    CAN_RX_CHANNEL);
	CANEnableFilter(CAN1, CAN_FILTER0, TRUE);

	/* Configuration finished, thus enter "Normal Operation" mode. */
	CANSetOperatingMode(CAN1, CAN_GET_MODE);
	while(CANGetOperatingMode(CAN1) != CAN_GET_MODE);

	return 0;
}

/*
 * Configures CAN module.
 *
 * format of configuration packet:
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
 */
static int
can_configure(char *data, int ln)
{
	int mode, speed;

	/* Validate packet length */
	if (ln != 6) {
		return EINVAL;
	}

	/* Validate configuration mode - only "01" supported for now. */
	if (data[0] != '0' || data[1] != '1') {
		return EINVAL;
	}

	/* Obtain mode. */
	mode = (int) hex2ulong(data + 2, 2);

	/* Obtain speed. */
	speed = (int) hex2ulong(data + 4, 2);

	/* Let the service finish process of configuration. */
	return can_set_mode_speed(mode, speed, TRUE);
}

/* global functions */
/*
 * Checks if configuration process is in progress.
 * Returns:
 *   TRUE if configuration is in progress; FALSE otherwise.
 */
BOOL
can_configuration_in_progress(void) {
	return (can_tx_st == CAN_TX_ST_CONF1 || can_tx_st == CAN_TX_ST_CONF2);
}

/*
 * Sets communication mode and speed.
 * Returns:
 *   0      - speed configured.
 *   EINVAL - specified mode or speed not supported.
 *   EBUSY  - module currently busy, speed can't be set.
 */
int
can_set_mode_speed(int mode, int speed, BOOL send_ack)
{
	/*
	 * Transmit service is used to apply configuration, so check if that
	 * service is currently available.
	 */
	if (can_tx_st != CAN_TX_ST_IDLE) {
		return EBUSY;
	}

	/* Mode is to be configured. */
	if (mode != -1) {
		if ((mode & 0x7f) > 4) {
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

	/* Instruct CAN to commence entering "Configuration" mode. */
	CANSetOperatingMode(CAN1, CAN_CONFIGURATION);

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
	/*
	 * Default communication mode
	 *  - "Normal Operation" mode
	 *  - snoop mode enabled
	 */
	CAN_SET_MODE(CAN_NORMAL_OPERATION);
	CAN_SNOOP_ENABLE;

	/* Default communication speed. */
	can_speed = SPEED_250k;

	/*
	 * Initialize CAN1 module.
	 */
	can_hw_init();

	/* Initialize state machine for receiving CAN frames. */
	can_rx_st = CAN_RX_ST_WAIT4PKT;

	/* Initialize state machine for transmitting CAN frames. */
	can_tx_st = CAN_TX_ST_IDLE;

	/* Set module ID for purposes of communication with master. */
	can_resp_ack_nak.id = MCOMM_CAN_ID;
	can_resp_data.id = MCOMM_CAN_ID;

	/* Register and enable RFID service. */
	rtl_svc_init("Can", can_svc, TRUE, FALSE);

	return 0;
}

/*
 * Suspend the module.
 */
int
can_suspend(void)
{
	/*
	 * Request CAN to enter "Disable" mode and wait
	 * until the switch is done.
	 */
	CANSetOperatingMode(CAN1, CAN_DISABLE);
	while(CANGetOperatingMode(CAN1) != CAN_DISABLE);

	return 0;
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
	fl_send_ack_nak = TRUE;

	/* Handle particular commands */
	switch (data[0]) {
		/* Configure the module. */
		case MCOMM_CAN_CMD_CFG:
			/* configure module */
			err = can_configure(data + 1, ln - 1);

			/*
			 * If configuration process has been succesfully
			 * commenced, ack/nak will be sent later -
			 * once the process of configuration is finished.
			 */
			if (err == 0) {
				fl_send_ack_nak = FALSE;
			}

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
		case MCOMM_CAN_CMD_GET_STATUS:
			/* Communication channel busy. */
			if (fl_send_data) {
				err = MCOM_NAK_MODULE_BUSY;
				translate_err = FALSE;
			}

			snprintf(can_resp_buf, sizeof (can_resp_buf),
			    "%02X%02X%02X",
			    ((CANGetErrorState(CAN1) >> 16) & 0x3f) |
			    (C1FIFOINT0bits.RXOVFLIF << 6),
			    CANGetTxErrorCount(CAN1),
			    CANGetRxErrorCount(CAN1));

			/* Send the message to the master. */
			can_send_data_response_to_master("00", can_resp_buf,
			    strlen(can_resp_buf));

			/* Data will be sent to master instead of ACK/NAK */
			fl_send_ack_nak = FALSE;
			break;

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
		mcom_prepare_ack_response(&can_resp_ack_nak);
	} else {
		/* Translate error code if appropriate. */
		if (translate_err)
			err = mcom_err_translate(err);

		mcom_prepare_nak_response(&can_resp_ack_nak, err);
	}

	return 0;
}

