/*********************************************************************
 *
 * Handles OBD communication layer with ECU. This layer is independent
 * on real communication protocol used (KWP2000, CAN ...).
 *
 *********************************************************************/

#include <errno.h>

#include "bt.h"
#include "can_hal.h"
#include "carmon.h"
#include "kwp2k.h"
#include "lcd_term.h"
#include "master_comm.h"
#include "obd.h"
#include "utils.h"
#include "rt_light.h"

/* local constants */
/* number of bytes per one DTC */
#define	OBD_BYTES_PER_DTC	2

/* number of DTCs per one frame */
#define	OBD_DTCS_PER_PKT	3

/* max time in us between dtc packets sent from ECU */
#define	OBD_DTC_PKT_TOUT	150000

/* local macros */
/* Returns TRUE if "freeze frame" data are requested; FALSE otherwise. */
#define	OBD_IS_FREEZE_FRAME	(obd_freeze_frame_num != -1)

/* Calculates packet number which contains specified DTC. */
#define	OBD_DTC_PKT_NUM(dtc_num)	((dtc_num) / OBD_DTCS_PER_PKT)

/* Calculates index in DTC cache which contains specified DTC. */
#define	OBD_DTC_CACHE_INDEX(dtc_num)	\
    (((dtc_num) % OBD_DTCS_PER_PKT) * OBD_BYTES_PER_DTC)

/* Returns TRUE if DTC is in DTC cache; FALSE otherwise. */
#define	OBD_DTC_IN_CACHE(dtc_num)	\
    ((obd_dtc_cache.pkt_num == OBD_DTC_PKT_NUM(dtc_num)) && \
    ((obd_dtc_cache.dtcs[OBD_DTC_CACHE_INDEX(dtc_num)] != 0) || \
    (obd_dtc_cache.dtcs[OBD_DTC_CACHE_INDEX(dtc_num) + 1] != 0)))

/* local variables */
/* Type of lower communication layer */
static OBD_COMM_MODE obd_comm_mode;

/* pointer to lower layer functions for sending/receiving OBD packet */
static int (*obd_send)(unsigned char *data, int ln);
static int (*obd_recv)(unsigned char **data, int *ln);

/*
 * Definition of PID groups utilized in autoscan mode.
 */
static obd_pid_groups_t	obd_pid_groups;

/* "freeze frame" number */
static int obd_freeze_frame_num;
/* TRUE activates auto-scan mode */
static BOOL obd_autoscan_run;
/* TRUE instructs OBD module to enter idle mode. */
static BOOL obd_goto_idle_flag;
/* If TRUE, ACK is sent to master after idle mode si entered. */
static BOOL obd_send_ack_when_idle;

/* OBD ECU state machine */
static enum {
	OBD_ECU_ST_IDLE = 0,	/* machine idle */
	OBD_ECU_ST_GETPID,	/* ask for PID value */
	OBD_ECU_ST_SENDPID,	/* send PID value to master */
	OBD_ECU_ST_NEXTPID,	/* move to the next PID */
	OBD_ECU_ST_WAIT4SAMPLETIME,	/* wait for sample time */
	OBD_ECU_ST_GETPERMDTC,	/* get confirmed DTC */
	OBD_ECU_ST_SENDDTC,	/* send DTC to master */
	OBD_ECU_ST_RESETDTC,	/* Reset/clear DTCs, freeze frames, ... */
	OBD_ECU_ST_SENDRESETDTCRES	/* Send result of reset operation */
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
	{ 0x2c1, 0x49, 6, 1, 0, 255, 202 }
};

/* List of structures holding PID values obtained via snooping CAN bus */
static struct obd_can_pid_value {
	BOOL	valid;			/* TRUE if data are valid */
	unsigned char	data[2];	/* PID data */	
} obd_can_pid_values[sizeof (obd_can_map_verso) /
    sizeof (struct obd_can_map)];

/* Sample rate for periodic PID acquisition */
static rtl_time_t obd_sr;
/*
 * Timer used
 *  - during periodic PID acquisition to measure sample time
 *  - to determine last packet when reading permanent DTCs
 */
static rtl_timer_t *obd_timer;

/* Number of DTC to be obtained. */
static int obd_dtc_num;

/* Caches up to three DTCs returned from ECU in form of one packet. */
static struct obd_dtc_cache_t {
	/* packet number */
	int	pkt_num;
	/* DTC chache */
	unsigned char	dtcs[OBD_DTCS_PER_PKT * OBD_BYTES_PER_DTC];
} obd_dtc_cache;

/* local functions */

static void obd_alarm_process(unsigned char *, int);

/*
 * Sends data packet to master.
 */
static int
obd_send_data_response_to_master(char *hdr, int ln)
{
	int err;

	err = mcom_prepare_generic_response(&obd_resp_data, hdr,
	    obd_resp_data_buf, MCOM_DATA_TYPE_BYTE, ln, MCOM_DONT_FRAGMENT);

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
obd_ecu_get_pid(int mode, int pid, unsigned char **d, int *ln)
{
	static BOOL in_progress = FALSE;

	int ret;
	int lln;

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

		/* Error occurred, abort further processing. */
		if (ret != 0) {
			return ret;
		}

		/*
		 * If PID number does not match, then discard
		 * just received packet and continue with receiving
		 * another one.
		 */
		if (obd_data[OBD_IS_FREEZE_FRAME ? 2 : 1] != pid) {
			in_progress = TRUE;

			return	EAGAIN;
		}

		/*
		 * Pass resulting data to the caller.
		 * Drop the header which is in form of
		 * [OBD_MODE_GET_CUR_PID | 0x40, pid] or
		 * [OBD_MODE_GET_FF_PID | 0x40, ff_num, pid].
		 */
		lln = OBD_IS_FREEZE_FRAME ? 3 : 2;
		*d = obd_data + lln;
		*ln = obd_ln - lln;

		/*
		 * Let current PID value to be processed
		 * by alarm framework. Pass PID number as well.
		 */
		if (!OBD_IS_FREEZE_FRAME) {
			obd_alarm_process(obd_data + 1, obd_ln - 1);
		}

		return 0;
	}

	/*
	 * If we are operating in KWP2000/CAN mixed mode and value of given
	 * PID is provided via CAN, do not ask ECU for the value.
	 * Instead use the one obtained via monitoring CAN bus.
	 * We do not check for OBD mode, since manufacturer specific
	 * PIDs are always provided on request.
	 * Also skip if "freeze frame" data are requested.
	 */
	if (!OBD_IS_FREEZE_FRAME && (obd_get_pid_from_can(pid, d, ln) == 0)) {
		return 0;
	}

	/* Start new request */
	if (OBD_IS_FREEZE_FRAME) {
		obd_packet_ecu[0] = OBD_MODE_GET_FF;
		obd_packet_ecu[1] = pid;
		obd_packet_ecu[2] = obd_freeze_frame_num;
		lln = 3;
	} else {
		obd_packet_ecu[0] = mode;
		obd_packet_ecu[1] = pid;
		lln = 2;
	}

	ret = obd_send(obd_packet_ecu, lln);

	/* Packet sent out successfully, wait for ECU response */
	if (ret == 0) {
		in_progress = TRUE;

		ret = EAGAIN;
	}

	return ret;
}

/*
 * Gets permanent/confirmed DTC.
 *
 * Returns:
 *        0 - DTC obtained
 *   EAGAIN - operation in progress
 *   EBUSY  - KWP2000 module busy - could not send the request
 *   EIO    - ECU did not respond or invalid response received
 *   EINVAL - DTC with specified number does not exist
 */
static int
obd_ecu_get_dtc(int dn, unsigned char **d, int *ln)
{
	static BOOL in_progress = FALSE;
	static int pktn;

	int i;
	int ret;

	/* Operation in progress, wait for data or error. */
	if (in_progress) {
		unsigned char *obd_data;
		int obd_ln;

		ret = obd_recv(&obd_data, &obd_ln);

		/*
		 * Timeout expired, no more DTC packets are to be received.
		 * Ignore timeout generated by obd_timer for the first response,
		 * since lower layer may need to establish communication
		 * with ECU which takes significantly longer.
		 */
		if (ret == EIO || (rtl_timer_expired(obd_timer) && pktn > 0)) {
			/* Operation finished. */
			in_progress = FALSE;

			/*
			 * Return DTC from cache or EINVAL if DTC not found
			 * in cache.
			 */
			if (!OBD_DTC_IN_CACHE(dn)) {
				return EINVAL;
			}

			*d = &obd_dtc_cache.dtcs[OBD_DTC_CACHE_INDEX(dn)];
			*ln = OBD_BYTES_PER_DTC;

			return 0;
		}

		/* Operation in progress, try again later. */
		if (ret == EAGAIN) {
			return EAGAIN;
		}

		/* Packet received, process it. */
		/*
		 * If this is a packet we are waiting for, populate DTC cache
		 * with data payload. One packet can carry up to three DTCs.
		 */
		if (OBD_DTC_PKT_NUM(dn) == pktn) {
			/* Number of DTCs in this packet. */
			int dtcs = (obd_ln - 1) / OBD_BYTES_PER_DTC;

			/* Populate the cache. */
			obd_dtc_cache.pkt_num = pktn;
			for (i = 0; i < dtcs * OBD_BYTES_PER_DTC; i++) {
				obd_dtc_cache.dtcs[i] = obd_data[1 + i];
			}
		}

		/* Wait for another DTC packet. */
		pktn++;

		/* Re-initialize timer measuring receive timeout. */
		rtl_timer_init(&obd_timer, OBD_DTC_PKT_TOUT);

		return EAGAIN;
	}

	/*
	 * We get here if new request is to be initiated.
	 * Before that, try to satisfy the request from DTC cache.
	 *
	 * Always go over the wire for the first DTC in the packet.
	 * That guarantees cache does not get stale (assuming DTCs
	 * are read sequentially).
	 */
	if ((OBD_DTC_CACHE_INDEX(dn) != 0) && OBD_DTC_IN_CACHE(dn)) {
		*d = &obd_dtc_cache.dtcs[OBD_DTC_CACHE_INDEX(dn)];
		*ln = OBD_BYTES_PER_DTC;

		return 0;
	}

	/*
	 * DTC not found in cache.
	 * Wipe out DTC cache and go over the wire to ECU for list of DTCs.
	 */
	for (i = 0; i < OBD_DTCS_PER_PKT; i++) {
		obd_dtc_cache.dtcs[i] = 0;
	}

	pktn = 0;

	/* Start new request */
	obd_packet_ecu[0] = OBD_MODE_GET_DTC;
	ret = obd_send(obd_packet_ecu, 1);

	/* Packet sent out successfully, wait for ECU response */
	if (ret == 0) {
		in_progress = TRUE;

		/* Initialize timer measuring receive timeout. */
		rtl_timer_init(&obd_timer, OBD_DTC_PKT_TOUT);

		ret = EAGAIN;
	}

	return ret;
}

/*
 * Reset/clear DTCs, freeze frames, ...
 *
 * Returns:
 *        0 - DTCs successfully cleared
 *   EAGAIN - operation in progress
 *   EBUSY  - KWP2000 module busy - could not send the request
 *   EIO    - ECU did not respond or invalid response received
 *   EINVAL - ECU indicated that conditions to clear DTCs are not met
 */
static int
obd_ecu_reset_dtc(void)
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

		/* We are done with waiting for response. */
		in_progress = FALSE;

		/* Operation failed, abort. */
		if (ret != 0) {
			return ret;
		}

		/* Packet received, process it. */
		if (obd_ln == 1 && obd_data[0] == (OBD_MODE_RESET_DTC | 0x40)) {
			/* Valid response received. */
			return 0;
		} else {
			/* Invalid response received. */
			return EINVAL;
		}
	}

	/* Send request to ECU. */
	obd_packet_ecu[0] = OBD_MODE_RESET_DTC;
	ret = obd_send(obd_packet_ecu, 1);

	/* Packet sent out successfully, wait for ECU response */
	if (ret == 0) {
		in_progress = TRUE;

		ret = EAGAIN;
	}

	return ret;
}

/*
 * Returns currently scanned PID number in autoscan mode.
 */
static int
obd_autoscan_get_curr_pid(void)
{
	return obd_pid_groups.list[obd_pid_groups.curr];
}

/*
 * Moves to the next PID in autoscan mode.
 */
static void
obd_autoscan_move_to_next_pid(void)
{
	int	i;

	/*
	 * Move to the next PID. As part of that process,
	 * re-calculate absolute position of current PID within PID list.
	 */
	obd_pid_groups.curr = 0;
	for (i = 0; i < OBD_PID_GROUP_NUM; i++) {
		obd_pid_groups.group[i].curr++;

		/* There is a PID to be processed in current group. */
		if (obd_pid_groups.group[i].curr <
		    obd_pid_groups.group[i].num) {
			obd_pid_groups.curr += obd_pid_groups.group[i].curr;

			break;
		}

		/*
		 * All PIDs from current group processed, reset
		 * 'current PID' pointer and jump to the next group.
		 */
		obd_pid_groups.group[i].curr = -1;
		obd_pid_groups.curr += obd_pid_groups.group[i].num;
	}

	/*
	 * If all PIDs in all groups processed, start from scratch next time.
	 * To determine that, check if 'current PID' points to the last
	 * PID in the list.
	 */
	if (obd_pid_groups.curr < obd_pid_groups.num - 1) {
		return;
	}

	/* All PIDs processed, start from scratch next time. */
	for (i = 0; i < OBD_PID_GROUP_NUM; i++) {
		obd_pid_groups.group[i].curr = -1;
	}

	return;
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
	static char hdr[8];

	int i, curr_pid, curr_mode;

	switch (obd_ecu_st) {
	/* machine idle */
	case OBD_ECU_ST_IDLE:
		/*
		 * If asked by master to enter idle mode, then let it
		 * know idle mode has just been entered.
		 */
		obd_goto_idle_flag = FALSE;

		if (obd_send_ack_when_idle) {
			obd_send_ack_when_idle = FALSE;
			obd_send_ack_response_to_master();
		}

		break;

	/* ask ECU for either current or "freeze frame" PID value */
	case OBD_ECU_ST_GETPID:
		curr_pid = obd_autoscan_get_curr_pid();
		curr_mode = OBD_GET_MODE_FROM_PID(curr_pid);

		/* Wait until pid value is received or error occurs. */
		ret = obd_ecu_get_pid(curr_mode, curr_pid, &d, &ln);

		/* Operation still in progress, try again later. */
		if (ret == EAGAIN) {
			break;
		}

		/* Operation finished, process the result. */

		/*
		 * Move to the next PID if operating in autonomous mode.
		 * Otherwise send result to the master first.
		 */
		if (cm_get_mode() == CM_MODE_AUTONOMOUS) {
			obd_ecu_st = OBD_ECU_ST_NEXTPID;
		} else {
			obd_ecu_st = OBD_ECU_ST_SENDPID;
		}

		break;

	/* Get permanent/confirmed DTC. */
	case OBD_ECU_ST_GETPERMDTC:
		/* Wait until DTC is received or error occurs. */
		ret = obd_ecu_get_dtc(obd_dtc_num, &d, &ln);

		/* Operation still in progress, try again later. */
		if (ret == EAGAIN) {
			break;
		}

		/* Operation finished, send result to master. */
		obd_ecu_st = OBD_ECU_ST_SENDDTC;

		break;

	/* Reset/clear DTCs, freeze frames, ... */
	case OBD_ECU_ST_RESETDTC:
		/* Wait until DTCs are clear. */
		ret = obd_ecu_reset_dtc();

		/* Operation still in progress, try again later. */
		if (ret == EAGAIN) {
			break;
		}

		/* Operation finished, send result to master. */
		obd_ecu_st = OBD_ECU_ST_SENDRESETDTCRES;

		break;

	/* Send PID value to master. */
	case OBD_ECU_ST_SENDPID:
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
			curr_pid = obd_autoscan_get_curr_pid();
			
			if (obd_autoscan_run) {
				snprintf(hdr, sizeof (hdr), "%c%02X%02X",
				    OBD_CMD_GETCURPIDS, pkt_num, curr_pid);

				/* Increment sequence number. */
				if (++pkt_num > 255)
					pkt_num = 1;
			} else {
				if (OBD_IS_FREEZE_FRAME) {
					snprintf(hdr, sizeof (hdr),
					    "%c00%02X%02X", OBD_CMD_GETFFPID,
					    obd_freeze_frame_num, curr_pid);
				} else {
					snprintf(hdr, sizeof (hdr), "%c00%02X",
					    OBD_CMD_GETCURPID, curr_pid);
				}
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

		obd_ecu_st = OBD_ECU_ST_NEXTPID;
		/* FALLTHROUGH */
	
	/* If operating in auto-scan mode, process next PID. */
	case OBD_ECU_ST_NEXTPID:
		if (obd_autoscan_run) {
			obd_autoscan_move_to_next_pid();

			obd_ecu_st = OBD_ECU_ST_WAIT4SAMPLETIME;
		} else {
			obd_ecu_st = OBD_ECU_ST_IDLE;
		}

		break;

	/* Send DTC to master. */
	case OBD_ECU_ST_SENDDTC:
		/* Wait until previous data response is sent to master. */
		if (mcom_response_inuse(&obd_resp_data)) {
			break;
		}

		/* DTC obtained, pass it to the master. */
		if (ret == 0) {
			/* Copy DTC to response data buffer. */
			for (i = 0; i < ln; i++) {
				obd_resp_data_buf[i] = d[i];
			}

			snprintf(hdr, sizeof (hdr), "%c00",
			    OBD_CMD_GETPERMDTC);

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

		obd_ecu_st = OBD_ECU_ST_IDLE;

		break;

	/* Send result of DTC reset operation to master. */
	case OBD_ECU_ST_SENDRESETDTCRES:
		/* Wait until previous data response is sent to master. */
		if (mcom_response_inuse(&obd_resp_data)) {
			break;
		}

		/* DTC reset operation succeeded, send ACK to master. */
		if (ret == 0) {
			obd_send_ack_response_to_master();
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

		obd_ecu_st = OBD_ECU_ST_IDLE;

		break;

	/*
	 * Wait until there is a time to acquire another sample.
	 * If meanwhile connection to master has been lost
	 * while operating in 'assisted mode' or if master decided
	 * to leave autoscan mode, now is the safe point to handle
	 * that transition.
	 */
	case OBD_ECU_ST_WAIT4SAMPLETIME:
		/*
		 * Stop auto-scan mode if either connection with master
		 * has been lost or if master expressed desire to leave
		 * auto-scan mode.
		 */
		if ((!bt_is_paired() && (cm_get_mode() == CM_MODE_ASSISTED))
		    || obd_goto_idle_flag) {
			obd_autoscan_conf_mode(FALSE);

			break;
		}

		if (!rtl_timer_expired(obd_timer))
			break;

		/* Re-initialize sample rate timer. */
		rtl_timer_init(&obd_timer, obd_sr);

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

/*
 * Alarm service.
 * Every time OBD layer receives current PID value,
 * that value is passed to alarm framework which
 * detects events interesting to user and indicates
 * (e.g. via heartbeat LED) such event occurred.
 */
static void
obd_alarm_process(unsigned char *d, int ln)
{
	static int warn_phase = 0;
	static int dpf_ar = 0;
	static int water = 0;
	int egt11, egt12;
	char *fb_addr;
	int fb_size;

	/* Take desired action based on PID value received. */
	switch (d[0]) {
	/*
	 * EGT bank 1
	 *
	 * Update terminal with new data.
	 *
	 * Inform user via LED if there is an active DPF regeneration
	 * currently in progress.
	 */
	case OBD_PID_EGT1:
		/*
		 * Extract value of EGT11 and EGT12 sensors from raw ECU data.
		 * Skip PID number and byte containing mask of supported EGT
		 * sensors. Result is in degree Celsius.
		 */
		egt11 = OBD_GET_EGT_VALUE(d + 2, 1);
		egt12 = OBD_GET_EGT_VALUE(d + 2, 2);

		/*
		 * State machine.
		 * Indicate EGT12 temperature on heartbeat LED.
		 * egt12 > 400 - LED blinking slowly
		 * egt12 > 500 - LED blinking quickly
		 * egt12 > 550 - LED permanently ON
		 */
		switch (warn_phase) {
		/* Initialization phase. */
		case 0:
			rtl_hb_mode(HB_MODE_OFF, 0, 0);
			warn_phase = 1;
			break;

		/* Warning off. */
		case 1:
			if (egt12 > 400) {
				rtl_hb_mode(HB_MODE_TICKS, 500, 1);

				warn_phase = 2;
			}
			break;

		/* Warning phase 1 - LED slowly blinking. */
		case 2:
			if (egt12 < 380) {
				rtl_hb_mode(HB_MODE_OFF, 0, 0);

				warn_phase = 1;
			}

			if (egt12 > 500) {
				rtl_hb_mode(HB_MODE_TICKS, 125, 1);

				warn_phase = 3;
			}
			break;

		/* Warning phase 2 - LED blinking quickly. */
		case 3:
			if (egt12 < 480) {
				rtl_hb_mode(HB_MODE_TICKS, 500, 1);

				warn_phase = 2;
			}

			if (egt12 > 550) {
				rtl_hb_mode(HB_MODE_ON, 0, 0);

				warn_phase = 4;
			}
			break;

		/* Warning phase 3 - LED permanently ON. */
		case 4:
			if (egt12 < 530) {
				rtl_hb_mode(HB_MODE_TICKS, 125, 1);

				warn_phase = 3;
			}
			break;

		/* Something went wrong - start from the scratch. */
		default:
			warn_phase = 0;
			break;
		} /* switch */

		/*
		 * Update terminal with new data
		 *  - temperature EGT11
		 *  - temperature EGT12
		 */

		/*
		 * Obtain information about framebuffer.
		 * If that fails, then terminal is unavailable,
		 * in which case there is no point to proceed
		 * with the update.
		 */
		if (lcdterm_fb_info(&fb_addr, &fb_size) != 0) {
			break;
		}

		/*
		 * Make sure string has exactly 16 characters
		 * (which is size of LCD display).
		 * If string was shorter null terminator would
		 * be displayed as well (as bogus character).
		 */
		snprintf(fb_addr, fb_size, " %3d%c %3d%c %3d%s ",
		    water, 0xb2, egt11, 0xb2, dpf_ar, "%");

		/* Write framebuffer to the terminal. */
		lcdterm_fb_write();

		break;

	/*
	 * DPF accumulation ratio.
	 *
	 * Just save the received value. It is later send to terminal
	 * when EGT information is obtained. 
	 */
	case OBD_PID_DPFAR:
		/* Pass pointer to the data payload (skip PID number). */
		dpf_ar = OBD_GET_DPFAR_VALUE(d + 1);
		break;

	/*
	 * Engine coolant temperature
	 *
	 * Just save the received value. It is later send to terminal
	 * when EGT information is obtained. 
	 */
	case OBD_PID_WATER:
		/* Pass pointer to the data payload (skip PID number). */
		water = OBD_GET_WATER_VALUE(d + 1);
		break;

	/* No action is to be taken for provided PID. */
	default:
		break;
	}
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
 * Configures sample rate for PID autoscan mode.
 * Returns:
 *        0 - configuration succeeded
 *   EINVAL - input data invalid
 */
int
obd_autoscan_conf_sample_rate(char *data)
{
	/* Get [ms] sample rate and convert it to [us]. */
	obd_sr = hex2ulong(data, 4) * 1000;

	return 0;
}

/*
 * Configures PID groups for autoscan mode.
 * Returns:
 *        0 - configuration succeeded
 *   EINVAL - input data invalid
 */
int
obd_autoscan_conf_pid_groups(char *data, int ln)
{
	int i;

	/*
	 * Populate group information and list of scanned PIDs.
	 * As part of that process, calculate total number
	 * of scanned PIDs.
	 */
	obd_pid_groups.num = 0;
	for (i = 0; i < OBD_PID_GROUP_NUM; i++) {
		/* Get number of PIDs in a group. */
		obd_pid_groups.group[i].num =
		    hex2ulong(&data[2 * i], 2);

		obd_pid_groups.num +=
		    obd_pid_groups.group[i].num;

		/* Initialize index of current PID */
		obd_pid_groups.group[i].curr = -1;
	}

	/* Start scan from the first PID in 'fast' group. */
	obd_autoscan_move_to_next_pid();

	/*
	 * The highest priority group must be never empty.
	 * Gaps are not supported either.
	 */
	if ((obd_pid_groups.group[0].num == 0) ||
	    ((obd_pid_groups.group[1].num == 0) &&
	    (obd_pid_groups.group[2].num != 0))) {
		return EINVAL;
	}

	/* Too many PIDs, abort. */
	if (obd_pid_groups.num > OBD_PID_LIST_LN) {
		return EINVAL;
	}

	/*
	 * Total number of PIDs does not match summ
	 * of PIDs in particular groups, abort.
	 */
	if (obd_pid_groups.num != (ln - 6) / 2) {
		return EINVAL;
	}

	hex2ucharbuf(&data[6], obd_pid_groups.list, ln - 6);

	/* Asking for current, not "freeze frame" data. */
	obd_freeze_frame_num = -1;

	return 0;
}

/*
 * Activates or deactivates PID autoscan mode.
 * returns: 0
 */
int
obd_autoscan_conf_mode(BOOL mode)
{
	if (mode) {
	/* Activate PID autoscan mode. */
		/* Initialize sample rate timer. */
		rtl_timer_init(&obd_timer, obd_sr);

		obd_ecu_st = OBD_ECU_ST_GETPID;
	} else {
	/* Deactivate PID autoscan mode. */
		obd_ecu_st = OBD_ECU_ST_IDLE;
	}

	obd_autoscan_run = mode;

	return 0;
}

/*
 * Instructs OBD module to enter idle mode.
 * returns: 0      - module entered idle mode 
 *          EAGAIN - process of switching to idle mode in-progress
 */
int
obd_goto_idle(void)
{
	/* Process already in-progress, return. */
	if (obd_goto_idle_flag) {
		return	EAGAIN;
	}

	/* Process finished. */
	if (obd_ecu_st == OBD_ECU_ST_IDLE) {
		return 0;
	}

	/* Process not started yet, so start it now. */
	obd_goto_idle_flag = TRUE;

	return EAGAIN;
}

/*
 * Returns TRUE if OBD module is currently IDLE.
 * FALSE otherwise.
 */
BOOL
obd_is_idle(void) {
	return (obd_ecu_st == OBD_ECU_ST_IDLE);
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

		/* Get "freeze frame" PID value */
		case OBD_CMD_GETFFPID:
			/* Module currently busy. */
			if (!obd_is_idle()) {
				err = EBUSY;
				respond_with_ack = FALSE;

				break;
			}

			/* We will obtain just one PID value. */
			obd_freeze_frame_num = hex2ulong(&data[2], 2);
			/* PID number */
			obd_pid_groups.list[0] = hex2ulong(&data[4], 2);

			/* Select the first PID in 'fast' group */
			obd_pid_groups.curr = 0;

			/* Let OBD service respond. */
			respond_now = FALSE;

			obd_ecu_st = OBD_ECU_ST_GETPID;

			break;

		/* Get current PID value */
		case OBD_CMD_GETCURPID:
			/* Module currently busy. */
			if (!obd_is_idle()) {
				err = EBUSY;
				respond_with_ack = FALSE;

				break;
			}

			/* We will obtain just one PID value. */
			/* Asking for current, not "freeze frame" data. */
			obd_freeze_frame_num = -1;
			/* mode - ignored for now - determined from PID */
			/* obd_pid_groups.list[0] = hex2ulong(&data[2], 2); */
			/* PID number */
			obd_pid_groups.list[0] = hex2ulong(&data[4], 2);

			/* Select the first PID in 'fast' group */
			obd_pid_groups.curr = 0;

			/* Let OBD service respond. */
			respond_now = FALSE;

			obd_ecu_st = OBD_ECU_ST_GETPID;

			break;

		/* Activate/deactivate PID auto-scan mode */
		case OBD_CMD_GETCURPIDS:
			/* PID list is empty. Deactivate auto-scan mode */
			if (ln == 2) {
				/*
				 * Postpone sending ACK to master,
				 * until module enters idle mode.
				 */
				if (obd_goto_idle() != 0) {
					respond_now = FALSE;

					obd_send_ack_when_idle = TRUE;
				}
				
				break;
			}

			/* Module currently busy. */
			if (!obd_is_idle()) {
				err = EBUSY;
				respond_with_ack = FALSE;

				break;
			}

			/* Set PID sample rate. */
			err = obd_autoscan_conf_sample_rate(&data[2]);
			if (err != 0) {
				respond_with_ack = FALSE;
				break;
			}

			/* Populate PID groups. */
			err = obd_autoscan_conf_pid_groups(&data[6], ln - 6);
			if (err != 0) {
				respond_with_ack = FALSE;
				break;
			}

			/* Activate PID autoscan mode. */
			obd_autoscan_conf_mode(TRUE);

			break;

		/* Get permanent/confirmed DTC. */
		case OBD_CMD_GETPERMDTC:
			/* DTC number to be read. */
			obd_dtc_num = hex2ulong(&data[2], 2);

			obd_ecu_st = OBD_ECU_ST_GETPERMDTC;

			/* Let OBD service respond. */
			respond_now = FALSE;

			break;

		/* Reset/clear DTCs, freeze frames, ... */
		case OBD_CMD_RESETDTC:
			obd_ecu_st = OBD_ECU_ST_RESETDTC;

			/* Let OBD service respond. */
			respond_now = FALSE;

			break;

		/* unknown command */
		default:
			err = MCOM_NAK_UNKNOWN_IDCMD;
			translate_err = FALSE;
			respond_with_ack = FALSE;

			break;
	} /* switch */

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
	obd_goto_idle_flag = FALSE;
	obd_send_ack_when_idle = FALSE;

	/* Initialize state machine sending response to master. */
	obd_comm_st = OBD_COMM_ST_IDLE;

	/* Set id of module in response structure. */
	mcom_response_init(&obd_resp_ack_nak, MCOMM_ECU_ID);
	mcom_response_init(&obd_resp_data, MCOMM_ECU_ID);

	/* auto-scan mode turned off */
	obd_autoscan_run = FALSE;

	/* Initialize packet sequence. */
	pkt_num = 1;

	/* Select CAN 11bit ID, 500kbit as default communication layer. */
	obd_set_comm_mode(OBD_COMM_MODE_CAN_SID_500);

	/* Set default sample rate during periodic PID acquisition to 50ms. */
	obd_sr = 50 * 1000;
	obd_timer = NULL;
	rtl_timer_init(&obd_timer, 0);

	/* register and enable OBD service */
	rtl_svc_init("Obd", obd_svc, TRUE, FALSE);

	return 0;
}

