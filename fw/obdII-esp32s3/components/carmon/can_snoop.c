/*********************************************************************
 *
 * CAN bus analyzer.
 *
 * When attaching a node to the CAN bus we don't know much about,
 * it is wise to analyze the existing bus parameters as well as traffic
 * before the node starts to actively participate in communication.
 * Or, we may just want to take a look at what's happening on some CAN bus.
 * As an example, modern cars use CAN bus to interconnect different electronic
 * units and we may want to poke around - just out of curiosity.
 *
 * Currently supported modes:
 *
 * Pass through snoop mode (01)
 * ----------------------------
 * In this mode, all received CAN frames are immediately passed to master.
 * It is possible to configure which part of frames are to be sent to master.
 * That is useful in scenarios when we are interested just in particular
 * information (e.g. IDs or data payload).
 * This mode honors previously configured acceptance filter, so it is
 * possible to focus on CAN frames with particular ID.
 * Complete format of CAN frame sent to master:
 *
 * --------------------------------------------------------------------
 * | SOF | C | R | 00 | TSTAMP | TYPE | ID | DLC | DATA | CHSUM | EOF |
 * --------------------------------------------------------------------
 *
 * where
 *
 * TSTAMP 2b    timestamp (1 ms resolution)
 * TYPE   1b    message type (bit 0 - RTR (1 for remote frame),
 *              bit 1 - EID (1 for extended ID)
 * ID     4b    29-bit extended ID (if TYPE[1] == 1)
 *              11-bit standard ID (if TYPE[1] == 0)
 * DLC    1b    data length code - 0..8
 * DATA   0..8b data
 *
 * Mode is activated in following way:
 *
 * -----------------------------------------
 * | SOF | C | A | 01 | MASK | CHSUM | EOF |
 * -----------------------------------------
 *
 * where
 * MASK 1b    Bitmask selecting which information is sent to master.
 *            Active if set to 1.
 *            bit 0 - TSTAMP
 *            bit 1 - TYPE
 *            bit 2 - ID
 *            bit 3 - DLC
 *            bit 4 - DATA
 *
 * Address analysis (02)
 * ---------------------
 * This mode is used to obtain information about IDs appearing on the bus.
 * Once activated, it starts listening to CAN bus and sends all new IDs found
 * to the master. This mode honors previously configured acceptance filter,
 * so it is possible to focus on CAN frames with just particular set of IDs.
 * Up to 100 addresses are scanned.
 * Format of CAN frame sent to master:
 *
 * ---------------------------------------
 * | SOF | C | R | 00 | ID | CHSUM | EOF |
 * ---------------------------------------
 *
 * where
 *
 * ID 4b 29-bit extended or 11-bit standard identifier;
 *       all ones if ID buffer is full
 *
 * Mode is activated in following way:
 *
 * ----------------------------------
 * | SOF | C | A | 02 | CHSUM | EOF |
 * ----------------------------------
 *
 * Data analysis (03)
 * ------------------
 * The next step of this drill-down type of analysis is process of determining
 * what kind of information particular data carry. To help with that,
 * data analysis mode provides for possibility to focus on particular
 * frame and analyzing changes in its data payload. The goal is to associate
 * those changes with changes in real world (e.g. change in speedometer value).
 * Frame is sent to master only if change in specified portion of data payload
 * is detected.
 * Format of CAN frame sent to master:
 *
 * ----------------------------------------------
 * | SOF | C | R | 00 | ID | DATA | CHSUM | EOF |
 * ----------------------------------------------
 *
 * where
 * ID    4b     29-bit extended or 11-bit standard identifier
 * DATA  0..8b  data payload
 *
 * Mode is activated in following way:
 *
 * -----------------------------------------
 * | SOF | C | A | 03 | MASK | CHSUM | EOF |
 * -----------------------------------------
 *
 * where
 * MASK 8b Bitmask selecting which bits of data payload are undergoing process
 *         of comparison. One means particular bit participates in the process.
 *
 * Mode 04 - timing analysis
 * -------------------------
 * Timing analysis provides information about timing situation on the bus.
 * In particular, it calculates number of frames received during analysis
 * as well as minimum and maximum time between two subsequent frames.
 * That mode can be either used to obtain overall picture about load on the bus
 * or (with combination of the filtering) it is possible to collect information
 * about frames with particular ID - e.g. frame rate as well as jitter can be
 * determined.
 * To commence the analysis, following frame is to be sent to master: 
 * 
 * ----------------------------------
 * | SOF | C | A | 04 | CHSUM | EOF |
 * ----------------------------------
 *
 * Once the analysis is finished by deactivation packet <CA00>,
 * results are sent to master in following form:
 *
 * ----------------------------------------------------------
 * | SOF | C | 00 | FNUM | TTIME | TMIN | TMAX |CHSUM | EOF |
 * ----------------------------------------------------------
 *
 * where
 * FNUM  4b total number of frames received during analysis
 * TTIME 2b total time of analysis - resolution 1ms
 * TMIN  2b minimum time between packets - resolution 100us
 * TMAX  2b maximum time between packets - resolution 100us
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
#include "can_snoop.h"
#include "rt_light.h"
#include "utils.h"

/* local constants */
/* bitmasks specifying what should be sent to master in pass-through mode */
#define CAN_SNOOP_PSMASK_TSTAMP	0x01	/* time-stamp */
#define CAN_SNOOP_PSMASK_TYPE	0x02	/* packet type */
#define CAN_SNOOP_PSMASK_ID	0x04	/* ID */
#define CAN_SNOOP_PSMASK_DLC	0x08	/* DLC */
#define CAN_SNOOP_PSMASK_DATA	0x10	/* data payload */
#define CAN_SNOOP_PSMASK_ALL	(CAN_SNOOP_PSMASK_TSTAMP | \
    CAN_SNOOP_PSMASK_TYPE | CAN_SNOOP_PSMASK_ID | \
    CAN_SNOOP_PSMASK_DLC | CAN_SNOOP_PSMASK_DATA)

/* size of ID list */
#define CAN_SNOOP_ID_LIST_SIZE		100

/* size of list of ID-data pairs */
#define CAN_SNOOP_DATA_LIST_SIZE	30

/* local types */
typedef enum {
	SNOOP_MODE_DISABLED,		/* 00 - finish current analysis */
	SNOOP_MODE_PASS_THROUGH,	/* 01 - pass through mode */
	SNOOP_MODE_COLLECT_IDS,		/* 02 - address analysis */
	SNOOP_MODE_DATA_ANALYSIS,	/* 03 - data analysis */
	SNOOP_MODE_TIME_ANALYSIS,	/* 04 - timing analysis */
	SNOOP_NUM_SUPPORTED_MODES
} CAN_SNOOP_MODE;

/* local variables */
/* Snooping mode */
static CAN_SNOOP_MODE can_snoop_mode;

/* Bitmask specifying which data are to be sent to master. */
static int can_snoop_frame_mask;

/*
 * Bitmask applicable during data analysis. It specifies which portion of data
 * payload is undergoing process of comparison.
 */
static BYTE can_snoop_data_mask[8];

/*
 * Number of packets dropped (not sent to master) due to high rate
 * of incoming CAN frames.
 */
static int can_snoop_num_dropped_frames;

/* response data buffer - must be large enough to hold the longest response. */
static char can_snoop_resp_buf[40];

/* result of address analysis */
static struct {
	int num;	/* number of objects stored in the object list. */

	/* List holding result of various analysis. */
	union {
		/* ID list */
		UINT32 id_list[CAN_SNOOP_ID_LIST_SIZE];

		/* List of ID-data pairs */
		struct {
			UINT32 id;
			BYTE data[8];
		} data_list[CAN_SNOOP_DATA_LIST_SIZE];
	};
} can_snoop_data;

/*
 * Result of timing analysis.
 * time values has resolution of 100us.
 */
static struct {
	UINT32 num;	/* number of packets received during analysis. */
	UINT32 ttotal;	/* total time of analysis. */

	UINT32 tmin;	/* min time between two frames. */
	UINT32 tmax;	/* max time between two frames. */

	UINT32 tlast;	/* time since last received packet. */
} can_snoop_time_anal;

/* local functions */
/*
 * Increments number of dropped frames.
 */
static void
can_snoop_inc_dropped_frames(void)
{
	if (can_snoop_num_dropped_frames < 255) {
		can_snoop_num_dropped_frames++;
	}

	return;
}

/*
 * Encodes received frame to ASCII string to be sent to master.
 *
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
static void
can_snoop_encode_response(CANRxMessageBuffer *rxm)
{
	int i;
	char tb[10];

	/* Clean up resulting buffer. */
	can_snoop_resp_buf[0] = '\0';

	/* Time-stamp */
	if (can_snoop_frame_mask & CAN_SNOOP_PSMASK_TSTAMP) {
		snprintf(tb, sizeof (tb), "%04X", rxm->msgSID.CMSGTS);
		strcat(can_snoop_resp_buf, tb);
	}

	/* Message type */
	if (can_snoop_frame_mask & CAN_SNOOP_PSMASK_TYPE) {
		snprintf(tb, sizeof (tb), "%02X",
		    rxm->msgEID.RTR | (rxm->msgEID.IDE << 1));

		strcat(can_snoop_resp_buf, tb);
	}

	/* ID */
	if (can_snoop_frame_mask & CAN_SNOOP_PSMASK_ID) {
		snprintf(tb, sizeof (tb), "%08X", CAN_GET_ID(rxm));

		strcat(can_snoop_resp_buf, tb);
	}

	/* DLC */
	if (can_snoop_frame_mask & CAN_SNOOP_PSMASK_DLC) {
		snprintf(tb, sizeof (tb), "%02X", rxm->msgEID.DLC);
		strcat(can_snoop_resp_buf, tb);
	}

	/* Add data payload. */
	if (can_snoop_frame_mask & CAN_SNOOP_PSMASK_DATA) {
		for (i = 0; i < rxm->msgEID.DLC; i++) {
			snprintf(tb, sizeof (tb), "%02X", rxm->data[i]);
			strcat(can_snoop_resp_buf, tb);
		}
	}
}

/*
 * Prepare message containing result of timing analysis.
 *
 * Format of the response data payload:
 *
 *  ------------------------------
 *  | FNUM | TTIME | TMIN | TMAX |
 *  ------------------------------
 *
 * where:
 *
 *  FNUM  number of frames received (4 bytes)
 *  TTIME total time of analysis
 *  TMIN  minimum time between two received frames (2 bytes)
 *  TMAX  maximum time between two received frames (2 bytes)
 * 
 */
static void
can_snoop_encode_time_anal_result(void)
{
	UINT32 ttime;
	char tb[10];

	/* Clean up resulting buffer. */
	can_snoop_resp_buf[0] = '\0';

	/* FNUM */
	snprintf(tb, sizeof (tb), "%08lX", can_snoop_time_anal.num);
	strcat(can_snoop_resp_buf, tb);

	/* TTIME - convert to 1ms resolution */
	ttime = can_snoop_time_anal.ttotal / (1000ULL / RTL_SERVICE_PERIOD);

	if (ttime > 0xffff) {
		ttime = 0xffff;
	}
	snprintf(tb, sizeof (tb), "%04lX", ttime);
	strcat(can_snoop_resp_buf, tb);

	/* TMIN */
	if (can_snoop_time_anal.tmin > 0xffff) {
		can_snoop_time_anal.tmin = 0xffff;
	}
	snprintf(tb, sizeof (tb), "%04lX", can_snoop_time_anal.tmin);
	strcat(can_snoop_resp_buf, tb);

	/* TMAX */
	if (can_snoop_time_anal.tmax > 0xffff) {
		can_snoop_time_anal.tmax = 0xffff;
	}
	snprintf(tb, sizeof (tb), "%04lX", can_snoop_time_anal.tmax);
	strcat(can_snoop_resp_buf, tb);
}

/*
 * Returns TRUE if there is analysis in progress; FALSE otherwise.
 */
static BOOL
can_snoop_busy(void)
{
	return (can_snoop_mode != SNOOP_MODE_DISABLED);
}

/*
 * Pass-through analysis.
 */
static void
can_snoop_pass_through(CANRxMessageBuffer *rxm)
{
	/*
	 * If module is now busy with sending previous response,
	 * discard the frame.
	 */
	if (can_response_inuse()) {
		can_snoop_inc_dropped_frames();
	} else {
		can_snoop_encode_response(rxm);

		/* Send the message to the master. */
		can_send_data_response_to_master("R00", can_snoop_resp_buf);
	}
}

/*
 * Address analysis.
 */
static void
can_snoop_collect_ids(CANRxMessageBuffer *rxm)
{
	int i;
	UINT32 id;

	/* Obtain ID from the packet. */
	id = CAN_GET_ID(rxm);

	/*
	 * If frame with new ID arrived, add ID to the list
	 * and send it to the master.
	 */
	for (i = 0; i < can_snoop_data.num &&
	    can_snoop_data.id_list[i] != id; i++)
		;

	/* Already known ID. */
	if (i < can_snoop_data.num) {
		return;
	}

	/*
	 * If the list is already full, inform master and terminate
	 * the analysis.
	 */
	if (can_snoop_data.num >= CAN_SNOOP_ID_LIST_SIZE) {
		/*
		 * Try to send the message to the master. If the attempt fails,
		 * retry when next frame arrives.
		 */
		if (can_response_inuse()) {
			can_snoop_inc_dropped_frames();

			return;
		} else {
			can_send_data_response_to_master("R00", "FFFFFFFF");
		}

		can_snoop_mode = SNOOP_MODE_DISABLED;

		return;
	}

	/*
	 * Try to send ID to the master. If the attempt fails, re-try when
	 * the same ID arrives again.
	 */
	if (can_response_inuse()) {
		can_snoop_inc_dropped_frames();

		return;
	} else {
		can_snoop_encode_response(rxm);

		can_send_data_response_to_master("R00", can_snoop_resp_buf);
	}

	/* Add ID to the list. */
	can_snoop_data.id_list[can_snoop_data.num++] = id;
}

/*
 * Data analysis.
 */
static void
can_snoop_data_analysis(CANRxMessageBuffer *rxm)
{
	int i, index;
	UINT32 id;
	BYTE *payload;
	BYTE ln;

	/* Obtain ID from the packet. */
	id = CAN_GET_ID(rxm);

	/* Get reference to data payload. */
	payload = rxm->data;

	/* Size of data payload. */
	ln = rxm->msgEID.DLC;

	/*
	 * If frame with new ID arrived, add it to the list
	 * and send it to the master.
	 */
	for (index = 0; index < can_snoop_data.num; index++) {
		/* ID found in the list, abort searching. */
		if (can_snoop_data.data_list[index].id == id) {
			break;
		}
	}

	/* Frame with new ID arrived. */
	if (index == can_snoop_data.num) {
		/* If there is no free space in the list, discard the frame. */
		if (index >= CAN_SNOOP_DATA_LIST_SIZE) {
			return;
		}

		/*
		 * Try to send the frame to the master. If the attempt fails,
		 * discard the frame.
		 */
		if (can_response_inuse()) {
			can_snoop_inc_dropped_frames();

			return;
		} else {
			can_snoop_encode_response(rxm);

			can_send_data_response_to_master("R00",
			    can_snoop_resp_buf);
		}

		/* Add new ID to the list */
		can_snoop_data.data_list[index].id = id;

		/* Adjust number of objects in the list. */
		can_snoop_data.num++;
	} else {
		/*
		 * Frame with known ID arrived. Send it to the master
		 * only if interesting portion of data payload changed.
		 */
		for (i = 0; i < ln; i++) {
			BYTE msk = can_snoop_data_mask[i];

			/* Change detected. */
			if ((payload[i] & msk) != 
			    (can_snoop_data.data_list[index].data[i] & msk)) {
				break;
			}
		}

		/* If frame has not changed, no action is to be taken. */
		if (i == ln) {
			return;
		}

		/*
		 * Try to send the frame to the master. If the attempt fails,
		 * discard the frame.
		 */
		if (can_response_inuse()) {
			can_snoop_inc_dropped_frames();

			return;
		} else {
			can_snoop_encode_response(rxm);

			can_send_data_response_to_master("R00",
			    can_snoop_resp_buf);
		}
	}

	/* Store new data payload to the list. */
	for (i = 0; i < ln; i++) {
		can_snoop_data.data_list[index].data[i] = payload[i];
	}
}

/*
 * Timing analysis.
 */
static void
can_snoop_time_analysis(CANRxMessageBuffer *rxm)
{
	UINT32 tdif;

	/*
	 * Do not touch the statistics if the timer has been frozen
	 * (to protect it from overflow).
	 */
	if (can_snoop_time_anal.ttotal >= 0xfffffffe) {
		return;
	}

	/* Increment total number of received frames. */
	can_snoop_time_anal.num++;

	/*
	 * If this is the first frame received, just capture time
	 * when the packet arrived.
	 */
	if (can_snoop_time_anal.tlast == 0xffffffff) {
		can_snoop_time_anal.tlast = can_snoop_time_anal.ttotal;

		return;
	}

	/* Calculate time passed since the last packet was received. */
	tdif = can_snoop_time_anal.ttotal - can_snoop_time_anal.tlast;

	/* Refresh time of last packet received. */
	can_snoop_time_anal.tlast = can_snoop_time_anal.ttotal;

	/* Re-calculate statistics. */
	/* Minimal time between two frames. */
	if (can_snoop_time_anal.tmin > tdif) {
		can_snoop_time_anal.tmin = tdif;
	}

	/* Maximal time between two frames. */
	if (can_snoop_time_anal.tmax < tdif) {
		can_snoop_time_anal.tmax = tdif;
	}
}

/* global functions */
/*
 * Initializes analyzer.
 */
void
can_snoop_init(void)
{
	/* Snooping disabled after power-on. */
	can_snoop_mode = SNOOP_MODE_DISABLED;

	can_snoop_num_dropped_frames = 0;

	/* If snooping enabled, everything is sent to master by default. */
	can_snoop_frame_mask = CAN_SNOOP_PSMASK_ALL;

	/* structure used during address analysis */
	can_snoop_data.num = 0;
}

/*
 * Returns number of dropped frames.
 */
int
can_snoop_get_num_dropped_frames(void)
{
	return can_snoop_num_dropped_frames;
}

/*
 * Processes master query.
 *
 * Returns:
 *  0      - snooping mode successfully configured
 *  EINVAL - invalid data provided
 *  EBUSY  - analysis already in progress
 */
int
can_snoop_process_query(char *data, int ln)
{
	CAN_SNOOP_MODE mode;
	int i, mask;

	mode = (CAN_SNOOP_MODE) hex2ulong(data, 2);

	/* Check that the requested mode is supported. */
	if ((mode < SNOOP_MODE_DISABLED) ||
	    (mode >= SNOOP_NUM_SUPPORTED_MODES)) {
		return EINVAL;
	}

	/*
	 * There is analysis in progress, the only acceptable
	 * change is finishing the current analysis.
	 */
	if (can_snoop_busy() && (mode != SNOOP_MODE_DISABLED)) {
		return EBUSY;
	}

	switch (mode) {
	case SNOOP_MODE_DISABLED:
		/*
		 * Timing analysis has been finished,
		 * thus send resulting data to master.
		 */
		if (can_snoop_mode == SNOOP_MODE_TIME_ANALYSIS) {
			can_snoop_encode_time_anal_result();

			/* Send the message to the master. */
			can_send_data_response_to_master("R00",
			    can_snoop_resp_buf);
		}

		break;

	/*
	 * Pass-through mode contains bitmask specifying which information
	 * is to be sent to master.
	 */
	case SNOOP_MODE_PASS_THROUGH:
		if (ln != 4) {
			return EINVAL;
		}

		mask = (int) hex2ulong(data + 2, 2);

		/* Validate given mask */
		if (mask & 0xe0) {
			return EINVAL;
		}

		can_snoop_frame_mask = mask;

		break;

	/*
	 * Initialize address analysis.
	 */
	case SNOOP_MODE_COLLECT_IDS:
		/* No more parameters expected. */
		if (ln != 2) {
			return EINVAL;
		}

		can_snoop_data.num = 0;

		/* Only ID is sent to master. */
		can_snoop_frame_mask = CAN_SNOOP_PSMASK_ID;

		break;

	/*
	 * Initialize data analysis.
	 */
	case SNOOP_MODE_DATA_ANALYSIS:
		/* Check if mandatory bitmask has been provided. */
		if (ln != 18) {
			return EINVAL;
		}

		/* Clean up the list of snoop objects. */
		can_snoop_data.num = 0;

		/* ID and data payload is to be sent to master. */
		can_snoop_frame_mask = CAN_SNOOP_PSMASK_ID |
		    CAN_SNOOP_PSMASK_DATA;

		/*
		 * Store bitmask specifying which portion of data payload
		 * will be undergoing process of comparison.
		 */
		for (i = 1; i <= 8; i++) {
			can_snoop_data_mask[i - 1] =
			    (BYTE) hex2ulong(data + 2 * i, 2);
		}

		break;

	/*
	 * Initialize timing analysis.
	 */
	case SNOOP_MODE_TIME_ANALYSIS:
		/* Reset number of packets received during analysis. */
		can_snoop_time_anal.num = 0;

		/* Reset total time of analysis. */
		can_snoop_time_anal.ttotal = 0;

		/* Reset min time between two frames. */
		can_snoop_time_anal.tmin = 0xffffffff;

		/* Reset max time between two frames. */
		can_snoop_time_anal.tmax = 0;

		/* Initialize time since last received packet. */
		can_snoop_time_anal.tlast = 0xffffffff;

		break;

	/* Make the compiler happy. */
	case SNOOP_NUM_SUPPORTED_MODES:
		break;
	} /* switch */

	/* Store current mode. */
	can_snoop_mode = mode;

	return 0;
}

/*
 * Lets received frame undergo process of analysis.
 */
int
can_snoop_process_frame(CANRxMessageBuffer *rxm)
{
	switch (can_snoop_mode) {
	/* Analysis disabled */
	case SNOOP_MODE_DISABLED:
		break;

	/* 01 - Pass through mode - each frame sent to master */
	case SNOOP_MODE_PASS_THROUGH:
		can_snoop_pass_through(rxm);
		break;

	/* 02 - Address analysis */
	case SNOOP_MODE_COLLECT_IDS:
		can_snoop_collect_ids(rxm);
		break;

	/* 03 - Data analysis */
	case SNOOP_MODE_DATA_ANALYSIS:
		can_snoop_data_analysis(rxm);
		break;

	/* 04 - Timing analysis */
	case SNOOP_MODE_TIME_ANALYSIS:
		can_snoop_time_analysis(rxm);
		break;

	/* Make the compiler happy. */
	case SNOOP_NUM_SUPPORTED_MODES:
		break;
	} /* switch */

	return 0;
}

/*
 * Snoop callback function. Called from CAN service. Handles snooping timers.
 */
void
can_snoop_cb(void)
{
	/*
	 * If timing analysis in progress, increment timer measuring
	 * total time of analysis.
	 */
	if (can_snoop_mode == SNOOP_MODE_TIME_ANALYSIS) {
		/* If we are approaching overflow, freeze the timer. */
		if (can_snoop_time_anal.ttotal >= 0xfffffffe) {
			return;
		}

		can_snoop_time_anal.ttotal++;
	}
}

