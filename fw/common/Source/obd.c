/*********************************************************************
 *
 * Handles OBD communication layer with ECU. This layer is independent
 * on real communication protocol used (KWP2000, CAN ...).
 *
 *********************************************************************/

#include <errno.h>

#include "bt.h"
#include "can_hal.h"
#include "kwp2k.h"
#include "master_comm.h"
#include "obd.h"
#include "utils.h"
#include "rt_light.h"

/* max number of PIDs scanned in auto-scan mode */
#define	OBD_PID_LIST_LN		100

/* local variables */
/* Type of lower communication layer */
static OBD_COMM_MODE obd_comm_mode;
/*
 * list of OBD PIDs which are scanned and their values sent periodically
 * to master
 */
static unsigned char obd_autoscan_pid_list[OBD_PID_LIST_LN];
/* number of PIDs scanned in auto-scan mode */
static int obd_autoscan_num;
/* index of currently scanned PID */
static int obd_autoscan_curr;
/* TRUE activates auto-scan mode */
static BOOL obd_autoscan_run;

/* OBD ECU state machine */
static enum {
	OBD_ECU_ST_IDLE = 0,	/* machine idle */
	OBD_ECU_ST_WAIT4PKT	/* wait for packet from ECU */
} obd_ecu_st;

/* packet to be sent to ECU */
static unsigned char obd_packet_ecu[OBD_ECU_PACKET_MAXLN];

/* communication module */
/* state machine handling sending response to master */
static enum {
	OBD_COMM_ST_IDLE = 0,		/* machine idle */
	OBD_COMM_ST_WAIT4KWP2KCONFDONE,	/* wait until KWP 2000 module is done
					   with configuration */
	OBD_COMM_ST_WAIT4CANCONFDONE	/* wait until CAN module is done
					   with configuration */
} obd_comm_st;
/* structure holding ACK/NAK response to master */
static mcom_response_t obd_resp_ack_nak;
/* If set to TRUE, send ack/nak response packet to master */
static BOOL fl_send_ack_nak;

/* structure holding data response to master */
static mcom_response_t obd_resp_data;
/* buffer holding response data payload */
static unsigned char obd_resp_data_buf[OBD_ECU_PACKET_MAXLN];
/* If set to TRUE, send response containing data to master */
static BOOL fl_send_data;

/* Packet sequence. Used by master to detect packet loss. */
static int pkt_num;
/* Packet header for PID autoscan mode - "Rnn" null-terminated string */
static char pkt_hdr[4];

/* local functions */

/*
 * Send query for PID value to ECU. Let OBD service take care
 * of sending response from ECU to master.
 */
static int
obd_ecu_get_pid(int pid)
{
	int err;

	/* prepare packet for ECU */
	obd_packet_ecu[0] = OBD_MODE_GET_PID;
	obd_packet_ecu[1] = pid;

	err = kwp2k_ul_send(obd_packet_ecu, 2);

	/* Packet sent out successfully, so wait for ECU response */
	if (err == 0)
		obd_ecu_st = OBD_ECU_ST_WAIT4PKT;

	return err;
}

/*
 * Handles OBD communication with ECU.
 */
static int
obd_comm_ecu_svc(void)
{
	int ln, i;
	int ret;
	int rx_err;
	unsigned char *d;

	switch (obd_ecu_st) {
	/* machine idle */
	case OBD_ECU_ST_IDLE:
		break;

	/* wait for packet from ECU */
	case OBD_ECU_ST_WAIT4PKT:
		ret = kwp2k_ul_recv(&d, &ln, &rx_err);

		/* packet being received now */
		if (ret == EAGAIN)
			break;

		/* packet received */
		if (ret == 0) {
			char *pkt_hdr_p = "P00";

			/*
			 * Copy PID and its value to response data buffer,
			 * discard header byte.
			 */
			for (i = 1; i < ln; i++)
				obd_resp_data_buf[i - 1] = d[i];

			/* Prepare data header for autoscan mode. */
			if (obd_autoscan_run) {
				snprintf(pkt_hdr, sizeof (pkt_hdr), "R%02X",
				    pkt_num);

				pkt_hdr_p = pkt_hdr;

				/* Increment sequence number. */
				if (++pkt_num > 255)
					pkt_num = 1;
			}

			mcom_prepare_generic_response(&obd_resp_data, pkt_hdr_p,
			    obd_resp_data_buf, MCOM_DATA_TYPE_BYTE, ln - 1);
		} else {
			obd_resp_data_buf[0] = (rx_err >> 8) & 0xff;
			obd_resp_data_buf[1] = rx_err & 0xff;

			/* error occurred */
			mcom_prepare_generic_response(&obd_resp_data,
			    MCOM_RESP_NAK, obd_resp_data_buf,
			    MCOM_DATA_TYPE_BYTE, 2);
		}

		/* Send packet to master */
		fl_send_data = TRUE;

		/*
		 * If connection with master has been lost, cancel auto-scan
		 * mode. Also switch to IDLE state if auto-scan is not enabled.
		 */
		if (!obd_autoscan_run || !bt_is_paired()) {
			obd_autoscan_run = FALSE;

			obd_ecu_st = OBD_ECU_ST_IDLE;
			break;
		}

		/* Move to the next PID */
		obd_autoscan_curr = obd_autoscan_curr == obd_autoscan_num - 1 ?
		    0 : obd_autoscan_curr + 1;

		/*
		 * If error occurs when sending out the query, inform master
		 * and disable auto-scan mode.
		 */
		ret = obd_ecu_get_pid(obd_autoscan_pid_list[obd_autoscan_curr]);

		if (ret != 0) {
			obd_resp_data_buf[0] = (ret >> 8) & 0xff;
			obd_resp_data_buf[1] = ret & 0xff;

			mcom_prepare_generic_response(&obd_resp_data,
			    MCOM_RESP_NAK, obd_resp_data_buf,
			    MCOM_DATA_TYPE_BYTE, 2);

			fl_send_data = TRUE;

			obd_autoscan_run = FALSE;

			obd_ecu_st = OBD_ECU_ST_IDLE;

			break;
		}

		break;
	}

	return 0;
}

/*
 * Takes care of sending response to master. Two responses might be scheduled
 * for transmission - ack/nak one and data one.
 * ack/nak response takes precedence over data response, since it is short,
 * thus it will occupy transmitter for shorter time.
 */
static int
obd_comm_master_svc(void)
{
	/* Wait for various conditions before sending ACK/NAK to master */
	switch (obd_comm_st) {
		case OBD_COMM_ST_IDLE:
			break;

		/* Wait until KWP 2000 module is done with configuration. */
		case OBD_COMM_ST_WAIT4KWP2KCONFDONE:
			/*
			 * For now configuration of KWP 2000 module
			 * is immediate, so no need to wait. Though
			 * it may change in future.
			 */
			mcom_prepare_ack_response(&obd_resp_ack_nak);
			fl_send_ack_nak = TRUE;

			obd_comm_st = OBD_COMM_ST_IDLE;
			break;

		/* Wait until CAN module is done with configuration. */
		case OBD_COMM_ST_WAIT4CANCONFDONE:
			if (can_configuration_in_progress())
				break;

			mcom_prepare_ack_response(&obd_resp_ack_nak);
			fl_send_ack_nak = TRUE;

			obd_comm_st = OBD_COMM_ST_IDLE;
			break;
	} /* switch */

	/*
	 * There is ack/nak response to be sent to master, attempt to queue it
	 * for transmission.
	 */
	if (fl_send_ack_nak) {
		if (mcom_queue_response(&obd_resp_ack_nak) != 0) {
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
		if (mcom_queue_response(&obd_resp_data) != 0) {
			return EAGAIN;
		}

		fl_send_data = FALSE;
	}

	return 0;
}


/*
 * Main service routine
 */
static int
obd_svc(void)
{

	/* Handle OBD communication with ECU */
	obd_comm_ecu_svc();

	/* Send response to master */
	obd_comm_master_svc();

	return 0;
}

/*
 * Selects communication mode.
 * Returns:
 *        0 - configuration process successfully commenced
 *   EINVAL - input data invalid
 */
static int
obd_set_comm_mode(OBD_COMM_MODE mode)
{
	int err = 0;
	CAN_COMM_SPEED can_speed;
	int filter_mode;
	UINT32 filter, mask;
	int kwp_mode = KWP2K_MODE_DEBUG;

	if ((mode < 0) || (mode >= OBD_COMM_MODE_NUMBER_OF_MODES))
		return EINVAL;

	switch (mode) {
	/* Auto mode - TODO */
	case OBD_COMM_MODE_AUTO:
		/* For now just make sure ACK is sent to master. */
		obd_comm_st = OBD_COMM_ST_WAIT4KWP2KCONFDONE;
		break;

	/* KWP 2000 normal */
	case OBD_COMM_MODE_KWP2K_NORM:
		kwp_mode = KWP2K_MODE_ENHANCED;

	/* KWP 2000 debug */
	case OBD_COMM_MODE_KWP2K_DEBUG:
		err = kwp2k_set_mode(kwp_mode);

		/*
		 * If KWP 2000 module successfully commenced changing the mode,
		 * wait until the switch is done, then send ACK to master.
		 */
		if (err == 0) {
			obd_comm_st = OBD_COMM_ST_WAIT4KWP2KCONFDONE;
		}

		break;

	/* Controller Area Network (CAN) */
	case OBD_COMM_MODE_CAN_SID_250:
	case OBD_COMM_MODE_CAN_EID_250:
	case OBD_COMM_MODE_CAN_SID_500:
	case OBD_COMM_MODE_CAN_EID_500:
		/* Configure speed. */
		if ((mode == OBD_COMM_MODE_CAN_SID_250) ||
		    (mode == OBD_COMM_MODE_CAN_EID_250)) {
			can_speed = SPEED_250k;
		} else {
			can_speed = SPEED_500k;
		}

		/* Configure acceptance filter. */
		if ((mode == OBD_COMM_MODE_CAN_SID_250) ||
		    (mode == OBD_COMM_MODE_CAN_SID_500)) {
			filter_mode = 0x01;
			filter = 0x7df;
			mask = 0x7c0;
		} else {
			filter_mode = 0x03;
			filter = 0x18daf100;
			mask = 0x1ff80000;
		}

		/*
		 * Configure CAN module:
		 *  - "Normal Operation" mode
		 *  - set communication speed
		 *  - configure acceptance filter
		 *  - instruct CAN module not to send ACK to master,
		 *    since OBD module will take care of it.
		 */
		err = can_configure(MODE_NORMAL, can_speed, filter_mode,
		    filter, mask, FALSE);

		/*
		 * If CAN module successfully commenced changing the mode,
		 * wait until the switch is done, then send ACK to master.
		 */
		if (err == 0) {
			obd_comm_st = OBD_COMM_ST_WAIT4CANCONFDONE;
		}

		break;

	/* Just to satisfy the compiler. */
	case OBD_COMM_MODE_NUMBER_OF_MODES:
		break;
	} /* switch */

	if (err == 0)
		obd_comm_mode = mode;

	return err;
}

/*
 * Configures the module.
 * Returns:
 *        0 - configuration process successfully commenced
 *   EINVAL - input data invalid
 */
static int
obd_conf(char *data, int ln)
{
	int mode, err;

	if (ln != 2)
		return EINVAL;

	mode = hex2ulong(data, 2);

	err = obd_set_comm_mode(mode);

	return err;
}

/* global functions */

/*
 * Processes query from master targeted to ECU module
 */
int
obd_process_query(char *data, int ln)
{
	int err;
	BOOL translate_err = TRUE;
	BOOL respond_with_ack = TRUE;
	BOOL respond_now = TRUE;

	/* Verify that the command is targeted to OBD submodule */
	if (data[0] != ECU_OBD_ID) {
		mcom_prepare_nak_response(&obd_resp_ack_nak,
		    MCOM_NAK_UNKNOWN_IDCMD);

		fl_send_ack_nak = TRUE;

		return 0;
	}

	/* Handle OBD commands */
	switch (data[1]) {
		/* Configure the module. */
		case OBD_CMD_CONF:
			err = obd_conf(&data[2], ln - 2);

			if (err != 0) {
				respond_with_ack = FALSE;
			} else {
				/*
				 * Let OBD service respond later once
				 * configuration process is finished.
				 */
				respond_now = FALSE;
			}

			break;

		/* Get PID value */
		case OBD_CMD_GETPID:
			err = obd_ecu_get_pid(hex2ulong(&data[2], 2));

			if (err != 0) {
				respond_with_ack = FALSE;
			} else {
				/* Let OBD service respond with PID value */
				respond_now = FALSE;
			}

			break;

		/* Activate/deactivate PID auto-scan mode */
		case OBD_CMD_GETPIDS:
			/* PID list is empty. deactivate auto-scan mode */
			if (ln == 2) {
				obd_autoscan_run = FALSE;
				break;
			}

			/*
			 * Initialize list of scanned PIDs
			 * Discard PIDs which exceed length of available buffer.
			 */
			obd_autoscan_num = (ln - 2) / 2;
			if (obd_autoscan_num > OBD_PID_LIST_LN)
				obd_autoscan_num = OBD_PID_LIST_LN;

			hex2ucharbuf(&data[2], obd_autoscan_pid_list, ln - 2);

			/* Current PID */
			obd_autoscan_curr = 0;

			/* Kick off the auto-scan process */
			err = obd_ecu_get_pid(obd_autoscan_pid_list[0]);

			/* If error occurred, do not activate auto-scan mode. */
			if (err == 0) {
				obd_autoscan_run = TRUE;
			} else {
				respond_with_ack = FALSE;
			}

			break;

		/* unknown command */
		default:
			err = MCOM_NAK_UNKNOWN_IDCMD;
			translate_err = FALSE;
			respond_with_ack = FALSE;

			break;
	}

	/* Do not respond now, OBD service will take care of that */
	if (!respond_now)
		return 0;

	/*
	 * Send response to master indicating that command was processed.
	 */
	if (respond_with_ack) {
		mcom_prepare_ack_response(&obd_resp_ack_nak);
	} else {
		/* Translate error code if appropriate. */
		if (translate_err)
			err = mcom_err_translate(err);

		mcom_prepare_nak_response(&obd_resp_ack_nak, err);
	}

	fl_send_ack_nak = TRUE;

	return 0;
}

/*
 * Initialization routine
 */
int
obd_init(void)
{
	/* Initialize ECU state machine */
	obd_ecu_st = OBD_ECU_ST_IDLE;

	/* Initialize state machine sending response to master. */
	obd_comm_st = OBD_COMM_ST_IDLE;

	/* Set id of module in response structure */
	obd_resp_data.id = obd_resp_ack_nak.id = MCOMM_ECU_ID;

	/* auto-scan mode turned off */
	obd_autoscan_run = FALSE;

	/* Initialize packet sequence. */
	pkt_num = 1;

	/* Select KWP2000 as default communication layer. */
	obd_set_comm_mode(OBD_COMM_MODE_KWP2K_NORM);

	/* register and enable OBD service */
	rtl_svc_init("Obd", obd_svc, TRUE, FALSE);

	return 0;
}

