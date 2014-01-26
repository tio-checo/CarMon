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
#define CAN_SNOOP_ID_LIST_SIZE	100

/* local macros */
/* extract ID from the message buffer */
#define CAN_DECODE_ID(frame) \
    ((frame)->msgEID.IDE ? ((frame)->msgSID.SID << 18) | (frame)->msgEID.EID : \
    (frame)->msgSID.SID)

/* local types */
typedef enum {
	SNOOP_MODE_DISABLED,		/* 00 - finish current analysis */
	SNOOP_MODE_PASS_THROUGH,	/* 01 - pass through mode */
	SNOOP_MODE_COLLECT_IDS,		/* 02 - address analysis */
	SNOOP_NUM_SUPPORTED_MODES
} CAN_SNOOP_MODE;

/* local variables */
/* Snooping mode */
static CAN_SNOOP_MODE can_snoop_mode;

/* Bitmask specifying which data are to be sent to master. */
static int can_snoop_data_mask;

/*
 * Number of packets dropped (not sent to master) due to high rate
 * of incoming CAN frames.
 */
static int can_snoop_num_dropped_frames;

/* response data buffer - must be large enough to hold the longest response. */
static char can_snoop_resp_buf[40];

/* structure used during address analysis */
static struct {
	int num;				/* number of collected IDs. */
	UINT32 list[CAN_SNOOP_ID_LIST_SIZE];	/* list of collected IDs. */
} can_snoop_collect_ids_data;

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
can_snoop_encode_frame(CANRxMessageBuffer *rxm)
{
	int i;
	char tb[10];

	/* Clean up resulting buffer. */
	can_snoop_resp_buf[0] = '\0';

	/* Time-stamp */
	if (can_snoop_data_mask & CAN_SNOOP_PSMASK_TSTAMP) {
		snprintf(tb, sizeof (tb), "%04X", rxm->msgSID.CMSGTS);
		strcat(can_snoop_resp_buf, tb);
	}

	/* Message type */
	if (can_snoop_data_mask & CAN_SNOOP_PSMASK_TYPE) {
		snprintf(tb, sizeof (tb), "%02X",
		    rxm->msgEID.RTR | (rxm->msgEID.IDE << 1));

		strcat(can_snoop_resp_buf, tb);
	}

	/* ID */
	if (can_snoop_data_mask & CAN_SNOOP_PSMASK_ID) {
		snprintf(tb, sizeof (tb), "%08X", CAN_DECODE_ID(rxm));

		strcat(can_snoop_resp_buf, tb);
	}

	/* DLC */
	if (can_snoop_data_mask & CAN_SNOOP_PSMASK_DLC) {
		snprintf(tb, sizeof (tb), "%02X", rxm->msgEID.DLC);
		strcat(can_snoop_resp_buf, tb);
	}

	/* Add data payload. */
	if (can_snoop_data_mask & CAN_SNOOP_PSMASK_DATA) {
		for (i = 0; i < rxm->msgEID.DLC; i++) {
			snprintf(tb, sizeof (tb), "%02X", rxm->data[i]);
			strcat(can_snoop_resp_buf, tb);
		}
	}
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
		can_snoop_encode_frame(rxm);

		/* Send the message to the master. */
		can_send_data_response_to_master("R00", can_snoop_resp_buf);
	}
}

/*
 * Adress analysis.
 */
static void
can_snoop_collect_ids(CANRxMessageBuffer *rxm)
{
	int i;
	UINT32 id;

	/* Obtain ID from the packet. */
	id = CAN_DECODE_ID(rxm);

	/*
	 * If frame with new ID arrived, add ID to the list
	 * and send it to the master.
	 */
	for (i = 0; i < can_snoop_collect_ids_data.num &&
	    can_snoop_collect_ids_data.list[i] != id; i++)
		;

	/* Already known ID. */
	if (i < can_snoop_collect_ids_data.num) {
		return;
	}

	/*
	 * If the list is already full, inform master and terminate
	 * the analysis.
	 */
	if (can_snoop_collect_ids_data.num >= CAN_SNOOP_ID_LIST_SIZE) {
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
		can_snoop_encode_frame(rxm);

		can_send_data_response_to_master("R00", can_snoop_resp_buf);
	}

	/* Add the ID to the list. */
	can_snoop_collect_ids_data.list[can_snoop_collect_ids_data.num++] = id;

	return;
}

/* global functions */
/*
 * Initializes analyzer.
 */
void
can_snoop_init(void)
{
	/* Pass-through mode enabled by default. */
	can_snoop_mode = SNOOP_MODE_PASS_THROUGH;

	can_snoop_num_dropped_frames = 0;

	/* Everything is sent to master by default. */
	can_snoop_data_mask = CAN_SNOOP_PSMASK_ALL;

	/* structure used during address analysis */
	can_snoop_collect_ids_data.num = 0;
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
 *       0 - snooping mode successfully configured
 *  EINVAL - invalid data provided
 *  EBUSY  - analysis already in progress
 */
int
can_snoop_process_query(char *data, int ln)
{
	CAN_SNOOP_MODE mode;

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

	/*
	 * Pass-through mode contains bitmask specifying which information
	 * is to be sent to master.
	 */
	if (mode == SNOOP_MODE_PASS_THROUGH) {
		int mask;

		if (ln != 4) {
			return EINVAL;
		}

		mask = (int) hex2ulong(data + 2, 2);

		/* Validate given mask */
		if (mask & 0xe0) {
			return EINVAL;
		}

		can_snoop_data_mask = mask;
	}

	/*
	 * Initialize address analysis.
	 */
	if (mode == SNOOP_MODE_COLLECT_IDS) {
		/* No more parameters expected. */
		if (ln != 2) {
			return EINVAL;
		}

		can_snoop_collect_ids_data.num = 0;

		/* Only ID is sent to master. */
		can_snoop_data_mask = CAN_SNOOP_PSMASK_ID;
	}

	/* Nothing to be done for now for disabled mode. */
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

	/* Make the compiler happy. */
	case SNOOP_NUM_SUPPORTED_MODES:
		break;
	} /* switch */

	return 0;
}
