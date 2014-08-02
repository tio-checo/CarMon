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

/* pointer to lower layer functions for sending/receiving OBD packet */
static int (*obd_send)(unsigned char *data, int ln);
static int (*obd_recv)(unsigned char **data, int *ln);

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
/* TRUE de-activates auto-scan mode */
static BOOL obd_autoscan_stop;

/* OBD ECU state machine */
static enum {
	OBD_ECU_ST_IDLE = 0,	/* machine idle */
	OBD_ECU_ST_GETPID,	/* ask for pid value */
	OBD_ECU_ST_SENDRESPONSE,	/* send response to master */
	OBD_ECU_ST_WAIT4SAMPLETIME	/* wait for sample time */
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
/* structure holding data response to master */
static mcom_response_t obd_resp_data;
/* buffer holding response data payload */
static unsigned char obd_resp_data_buf[OBD_ECU_PACKET_MAXLN];

/* Packet sequence. Used by master to detect packet loss. */
static int pkt_num;

/* Mapping between CAN frames (Toyota 2AD-FTV engine) and OBD PIDs. */
static const struct obd_can_map {
	int		canid;	/* CAN ID - SID in this particular case */
	unsigned char	pid;	/* OBD PID */
	unsigned char	offset;	/* position of PID data within CAN frame */
	unsigned char	ln;	/* number of data bytes representing PID */
	long		add;	/* additive constant */
	long		mul;	/* multiplication constant */
	long		div;	/* division constant */
} obd_can_map_verso[] = {
	/* Engine RPM */
	{ 0x2c4, 0x0c, 0, 2, 0, 4 * 1000, 1285 },
	/* Accelerator pedal position D */
	{ 0x2c1, 0x49, 6, 1, 0, 255, 200 }
};

/* List of structures holding PID values obtained via snooping CAN bus */
static struct obd_can_pid_value {
	BOOL	valid;			/* TRUE if data are valid */
	unsigned char	data[2];	/* PID data */	
} obd_can_pid_values[sizeof (obd_can_map_verso) /
    sizeof (struct obd_can_map)];

/* Sample rate for periodic PID acquisition */
static rtl_time_t obd_sr;
/* Sample rate timer used during periodic PID acquisition */
static rtl_timer_t *obd_sr_timer;

/* local functions */

/*
 * Sends data packet to master.
 */
static int
obd_send_data_response_to_master(char *hdr, int ln)
{
	int err;

	err = mcom_prepare_generic_response(&obd_resp_data, hdr,
	    obd_resp_data_buf, MCOM_DATA_TYPE_BYTE, ln);

	if (err == 0) {
		mcom_queue_response(&obd_resp_data);
	}

	return err;
}

/*
 * Sends ACK to master.
 */
static int
obd_send_ack_response_to_master(void)
{
	
	int err;

	err = mcom_prepare_ack_response(&obd_resp_ack_nak);

	if (err == 0) {
		mcom_queue_response(&obd_resp_ack_nak);
	}

	return err;
}

/*
 * Sends NAK to master.
 */
static int
obd_send_nak_response_to_master(unsigned char e)
{
	int err;

	err = mcom_prepare_nak_response(&obd_resp_ack_nak, e);

	if (err == 0) {
		mcom_queue_response(&obd_resp_ack_nak);
	}

	return err;
}

/*
 * Tries to obtain value of given PID from CAN.
 *
 * Returns:
 *        0 - PID value successfully obtained
 *  ENOTSUP - CAN does not support given PID
 */
static int
obd_get_pid_from_can(int pid, unsigned char **value, int *ln)
{
	int i;
	int mapln = sizeof (obd_can_map_verso) / sizeof (struct obd_can_map);
	const struct obd_can_map *map = obd_can_map_verso;

	/* Not operating neither in mixed mode nor in debug mode, return. */
	if ((obd_comm_mode != OBD_COMM_MODE_KWP2K_MIXED) &&
	    (obd_comm_mode != OBD_COMM_MODE_KWP2K_DEBUG)) {
		return ENOTSUP;
	}

	/* Search for given PID in the map. */
	for (i = 0; (map[i].pid != pid) && (i < mapln); i++)
		;

	/* PID not supported. */
	if (i >= mapln) {
		return ENOTSUP;
	}

	/*
	 * PID found. If it contains valid data,
	 * return pointers to data and size.
	 */
	if (!obd_can_pid_values[i].valid) {
		return ENOTSUP;
	}

	*value = obd_can_pid_values[i].data;
	*ln = map[i].ln;

	return 0;
}

/*
 * Gets PID value.
 *
 * Returns:
 *        0 - PID value obtained
 *   EAGAIN - operation in progress
 *   EBUSY  - KWP2000 module busy - could not send the request
 *   EIO    - ECU did not respond or invalid response received
 */
static int
obd_ecu_get_pid(int pid, unsigned char **d, int *ln)
{
	static BOOL in_progress = FALSE;

	int ret;

	/* Operation in progress, wait for data or error. */
	if (in_progress) {
		unsigned char *obd_data;
		int obd_ln;

		ret = obd_recv(&obd_data, &obd_ln);

		/* Operation in progress, try again later. */
		if (ret == EAGAIN) {
			return EAGAIN;
		}

		/* Operation finished. */
		in_progress = FALSE;

		/*
		 * Pass resulting data to the caller.
		 * Drop the header which is in form of
		 * [OBD_MODE_GET_PID | 0x40, pid].
		 */
		*d = obd_data + 2;
		*ln = obd_ln - 2;

		return ret;
	}

	/*
	 * If we are operating in KWP2000/CAN mixed mode and value of given
	 * PID is provided via CAN, do not ask ECU for the value.
	 * Instead use the one obtained via monitoring CAN bus.
	 */
	if (obd_get_pid_from_can(pid, d, ln) == 0) {
		return 0;
	}

	/* Start new request */
	obd_packet_ecu[0] = OBD_MODE_GET_PID;
	obd_packet_ecu[1] = pid;

	ret = obd_send(obd_packet_ecu, 2);

	/* Packet sent out successfully, wait for ECU response */
	if (ret == 0) {
		in_progress = TRUE;

		ret = EAGAIN;
	}

	return ret;
}

/*
 * Handles OBD communication with ECU.
 */
static int
obd_comm_ecu_svc(void)
{
	static int ret;
	static int ln;
	static unsigned char *d;
	static char hdr[6];

	int i, curr_pid;

	switch (obd_ecu_st) {
	/* machine idle */
	case OBD_ECU_ST_IDLE:
		break;

	/* ask ECU for pid value */
	case OBD_ECU_ST_GETPID:
		curr_pid = obd_autoscan_pid_list[obd_autoscan_curr];

		/* Wait until pid value is received or error occurs. */
		ret = obd_ecu_get_pid(curr_pid, &d, &ln);

		/* Operation still in progress, try again later. */
		if (ret == EAGAIN) {
			break;
		}

		/* Operation finished, process the result. */

		/*
		 * Connection with master lost - discard obtained data
		 * and disable auto-scan mode.
		 */
		if (!bt_is_paired()) {
			obd_autoscan_run = FALSE;

			obd_ecu_st = OBD_ECU_ST_IDLE;
		} else {
			/* Send result to master. */
			obd_ecu_st = OBD_ECU_ST_SENDRESPONSE;
		}

		break;

	/* Send response to master. */
	case OBD_ECU_ST_SENDRESPONSE:
		/* Wait until previous data response is sent to master. */
		if (mcom_response_inuse(&obd_resp_data)) {
			break;
		}

		/* PID value obtained, pass it to the master. */
		if (ret == 0) {
			/* Copy PID value to response data buffer. */
			for (i = 0; i < ln; i++) {
				obd_resp_data_buf[i] = d[i];
			}

			/*
			 * Prepare data header - sequence number is incremented
			 * in auto-scan mode.
			 */
			curr_pid = obd_autoscan_pid_list[obd_autoscan_curr];
			if (obd_autoscan_run) {
				snprintf(hdr, sizeof (hdr), "R%02X%02X",
				    pkt_num, curr_pid);

				/* Increment sequence number. */
				if (++pkt_num > 255)
					pkt_num = 1;
			} else {
				snprintf(hdr, sizeof (hdr), "P00%02X",
				    curr_pid);
			}

			obd_send_data_response_to_master(hdr, ln);
		} else {
		/* Send NAK to master. */
			obd_resp_data_buf[0] = mcom_err_translate(ret) & 0xff;

			/*
			 * Using obd_resp_data to send NAK to master, since
			 * we are sure it is available. Thus, no need to
			 * check if the operation succeeded.
			 */
			obd_send_data_response_to_master(MCOM_RESP_NAK, 1);
		}

		/* Upper layer de-activated auto-scan mode. */
		if (obd_autoscan_stop) {
			obd_autoscan_stop = FALSE;
			obd_autoscan_run = FALSE;
		}

		/* If operating in auto-scan mode, process next PID. */
		if (obd_autoscan_run) {
			obd_autoscan_curr = obd_autoscan_curr ==
			    obd_autoscan_num - 1 ? 0 :
			    obd_autoscan_curr + 1;

			obd_ecu_st = OBD_ECU_ST_WAIT4SAMPLETIME;
		} else {
			obd_ecu_st = OBD_ECU_ST_IDLE;
		}

		break;

	/* Wait until there is a time to acquire another sample. */
	case OBD_ECU_ST_WAIT4SAMPLETIME:
		if (!rtl_timer_expired(obd_sr_timer))
			break;

		/* Re-initialize sample rate timer. */
		rtl_timer_init(&obd_sr_timer, obd_sr);

		/* Ask ECU for another PID value. */
		obd_ecu_st = OBD_ECU_ST_GETPID;

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
			obd_send_ack_response_to_master();

			obd_comm_st = OBD_COMM_ST_IDLE;
			break;

		/* Wait until CAN module is done with configuration. */
		case OBD_COMM_ST_WAIT4CANCONFDONE:
			if (can_configuration_in_progress())
				break;

			obd_send_ack_response_to_master();

			obd_comm_st = OBD_COMM_ST_IDLE;
			break;
	} /* switch */

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
		/* obd_[send|recv] must be initialized to something. */
		obd_send = kwp2k_ul_send;
		obd_recv = kwp2k_obd_recv;

		/* For now just make sure ACK is sent to master. */
		obd_comm_st = OBD_COMM_ST_WAIT4KWP2KCONFDONE;
		break;

	/* KWP2000 normal or mixed with CAN */
	case OBD_COMM_MODE_KWP2K_NORM:
	case OBD_COMM_MODE_KWP2K_MIXED:
		kwp_mode = KWP2K_MODE_ENHANCED;

	/* KWP 2000 debug */
	case OBD_COMM_MODE_KWP2K_DEBUG:
		/*
		 * Initialize pointers to functions for sending and receiving
		 * OBD packets.
		 */
		obd_send = kwp2k_ul_send;
		obd_recv = kwp2k_obd_recv;

		err = kwp2k_set_mode(kwp_mode);

		/* Failed to configure KWP2000 mode, do not proceed. */
		if (err != 0) {
			break;
		}

		/*
		 * Not operating in KWP2000/CAN mixed mode, so just wait
		 * until KWP2000 configuration is finished.
		 */
		if (mode != OBD_COMM_MODE_KWP2K_MIXED) {
			obd_comm_st = OBD_COMM_ST_WAIT4KWP2KCONFDONE;

			break;
		}

		/*
		 * Operating in mixed mode, configure CAN as well.
		 * Accept frames with SID addresses 0x2C0 - 0x2CF.
		 */
		err = can_configure(MODE_LISTEN_ONLY, SPEED_500k, 0x01,
		    0x2c0, 0x7f0, FALSE);

		/*
		 * If CAN module successfully commenced changing the mode,
		 * wait until the switch is done, then send ACK to master.
		 */
		if (err == 0) {
			obd_comm_st = OBD_COMM_ST_WAIT4CANCONFDONE;
		}

		break;

	/* Controller Area Network (CAN) */
	case OBD_COMM_MODE_CAN_SID_250:
	case OBD_COMM_MODE_CAN_EID_250:
	case OBD_COMM_MODE_CAN_SID_500:
	case OBD_COMM_MODE_CAN_EID_500:
		/*
		 * Initialize pointers to functions for sending and receiving
		 * OBD packets.
		 */
		obd_send = can_obd_send;
		obd_recv = can_obd_recv;

		/* Configure speed. */
		if ((mode == OBD_COMM_MODE_CAN_SID_250) ||
		    (mode == OBD_COMM_MODE_CAN_EID_250)) {
			can_speed = SPEED_250k;
		} else {
			can_speed = SPEED_500k;
		}

		/*
		 * Configure acceptance filter to accept diagnostic messages.
		 *
		 * In case of SID tester sends frames with diagnostic
		 * functional address 0x7DF. Besides that ECUs also accept
		 * 0x7E0 - 0x7E7 range of physical addresses and respond
		 * with physical addresses in range of 0x7E8 - 0x7EF.
		 *
		 * For EID, following CAN IDs are specified by standard:
		 *   0x18DB33F1 - CAN identifier for functionally address
		 *                request messages sent by the external
		 *                test equipment.
		 *   0x18DAxxF1 - Physical request CAN identifier
		 *                from the external test equipment to ECU #xx.
		 *   0x18DAF1xx - Physical response CAN identifier from ECU #xx
		 *                to the external test equipment.
		 */
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

	if (err == 0) {
		obd_comm_mode = mode;
	}

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
 * Callback routine processing received CAN frames.
 */
int
obd_can_rx_cb(CANRxMessageBuffer *rxm)
{
	int i, j;
	int id = CAN_GET_ID(rxm);
	int mapln = sizeof (obd_can_map_verso) / sizeof (struct obd_can_map);
	const struct obd_can_map *map = obd_can_map_verso;

	/*
	 * There is nothing to do if we are not operating
	 * in KWP2000/CAN mixed mode.
	 */
	if (obd_comm_mode != OBD_COMM_MODE_KWP2K_MIXED) {
		return 0;
	}

	/* Populate OBD PIDs from given frame. */
	for (i = 0; i < mapln; i++) {
		long value = 0;

		/* Frame not interesting, skip it. */
		if (map[i].canid != id) {
			continue;
		}

		/* Transform data from raw CAN data to OBD PID. */
		for (j = 0; j < map[i].ln; j++) {
			value *= 256L;
			value += rxm->data[map[i].offset + j];
		}

		value += map[i].add;
		value *= map[i].mul;
		value /= map[i].div;

		for (j = map[i].ln - 1; j >= 0; j--) {
			obd_can_pid_values[i].data[j] = value % 256L;
			value /= 256L;
		}

		/* From now consider the data valid. */
		obd_can_pid_values[i].valid = TRUE;
	}

	return 0;
}

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
		obd_send_nak_response_to_master(MCOM_NAK_UNKNOWN_IDCMD);

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
			/* Module currently busy. */
			if (obd_ecu_st != OBD_ECU_ST_IDLE) {
				err = EBUSY;
				respond_with_ack = FALSE;

				break;
			}

			/* We will obtain just one PID value. */
			obd_autoscan_num = 1;
			obd_autoscan_curr = 0;
			obd_autoscan_pid_list[0] = hex2ulong(&data[2], 2);

			/* Let OBD service respond. */
			respond_now = FALSE;

			obd_ecu_st = OBD_ECU_ST_GETPID;

			break;

		/* Activate/deactivate PID auto-scan mode */
		case OBD_CMD_GETPIDS:
			/* PID list is empty. deactivate auto-scan mode */
			if (ln == 2) {
				obd_autoscan_stop = TRUE;
				break;
			}

			/* Module currently busy. */
			if (obd_ecu_st != OBD_ECU_ST_IDLE) {
				err = EBUSY;
				respond_with_ack = FALSE;

				break;
			}

			/* Get [ms] sample rate and convert it to [us]. */
			obd_sr = hex2ulong(&data[2], 4) * 1000;

			/*
			 * Initialize list of scanned PIDs - skip first six
			 * ASCII characters - header and sample rate.
			 * Discard PIDs which exceed length of available buffer.
			 */
			obd_autoscan_num = (ln - 6) / 2;
			if (obd_autoscan_num > OBD_PID_LIST_LN)
				obd_autoscan_num = OBD_PID_LIST_LN;

			hex2ucharbuf(&data[6], obd_autoscan_pid_list, ln - 6);

			/* Current PID */
			obd_autoscan_curr = 0;

			/* Activate auto-scan mode. */
			obd_autoscan_run = TRUE;
			obd_autoscan_stop = FALSE;

			/* Initialize sample rate timer. */
			rtl_timer_init(&obd_sr_timer, obd_sr);

			obd_ecu_st = OBD_ECU_ST_GETPID;

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
		obd_send_ack_response_to_master();
	} else {
		/* Translate error code if appropriate. */
		if (translate_err)
			err = mcom_err_translate(err);

		obd_send_nak_response_to_master(err);
	}

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
	mcom_response_init(&obd_resp_ack_nak, MCOMM_ECU_ID);
	mcom_response_init(&obd_resp_data, MCOMM_ECU_ID);

	/* auto-scan mode turned off */
	obd_autoscan_run = FALSE;
	obd_autoscan_stop = FALSE;

	/* Initialize packet sequence. */
	pkt_num = 1;

	/* Select CAN 11bit ID, 500kbit as default communication layer. */
	obd_set_comm_mode(OBD_COMM_MODE_CAN_SID_500);

	/* Set default sample rate during periodic PID acquisition to 50ms. */
	obd_sr = 50 * 1000;
	obd_sr_timer = NULL;
	rtl_timer_init(&obd_sr_timer, 0);

	/* register and enable OBD service */
	rtl_svc_init("Obd", obd_svc, TRUE, FALSE);

	return 0;
}

