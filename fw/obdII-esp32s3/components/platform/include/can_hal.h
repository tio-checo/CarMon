/*********************************************************************
 *
 * Support for Controller Area Network (CAN) PIC module - provides
 * API for master device.
 *
 *********************************************************************/

#ifndef __CAN_HAL_H
#define __CAN_HAL_H

#include <stdint.h>
#include "GenericTypeDefs.h"

/* constants */
/* CAN module commands and responses */
/* analyze the bus  */
#define	MCOMM_CAN_CMD_SNOOP		'A'
/* configure the module  */
#define	MCOMM_CAN_CMD_CFG		'C'
/* get status of the module */
#define	MCOMM_CAN_CMD_GET_STATUS	's'
/* send packet */
#define	MCOMM_CAN_CMD_SEND_FRAME	'S'
/* packet received */
#define	MCOMM_CAN_RSP_FRAME_RECEIVED	'R'

/* macros */
/* extract ID from the message buffer */
#define CAN_GET_ID(frame) \
    ((frame)->msgEID.IDE ? ((frame)->msgSID.SID << 18) | (frame)->msgEID.EID : \
    (frame)->msgSID.SID)

/* types */
/*
 * Logical CAN frame, independent of the controller message RAM layout.
 * Backends populate the fields explicitly.
 */
typedef struct {
	struct {
		uint32_t SID;
		uint32_t CMSGTS;
	} msgSID;
	struct {
		uint32_t EID;
		uint32_t IDE;
		uint32_t RTR;
		uint32_t DLC;
	} msgEID;
	uint8_t data[8];
} can_frame_t;

/* supported modes */
typedef enum {
	MODE_NORMAL,
	MODE_DISABLED,
	MODE_LOOPBACK,
	MODE_LISTEN_ONLY,
	MODE_LISTEN_ALL_MESSAGES,
	CAN_NUM_SUPPORTED_MODES
} CAN_COMM_MODE;

/* supported speeds */
typedef enum {
	SPEED_1M,
	SPEED_500k,
	SPEED_250k,
	SPEED_125k,
	CAN_NUM_SUPPORTED_SPEEDS
} CAN_COMM_SPEED;

/* public functions */
/* Initialization routine. */
extern int can_init(void);

/* Suspend the module. */
extern int can_suspend(void);

/* Processes query from master targeted to CAN module */
extern int can_process_query(char *, int);

/* Sends data packet to master. */
extern int can_send_data_response_to_master(char *, char *);

/* Returns TRUE if current data response is in use; FALSE otherwise. */
extern BOOL can_response_inuse(void);

/*
 * Configure the CAN module synchronously.
 * Called from task context with exclusive access to CAN.
 * Does not send protocol responses.
 *
 * Returns:
 *   0       Configuration applied successfully.
 *   EINVAL  Unsupported mode, speed, or filter configuration.
 *   EIO     Controller could not be stopped, created, or configured.
 */
extern int can_configure(int, int, int, UINT32, UINT32);

/*
 * Receive one matching CAN frame.
 * Called from task context with exclusive access to CAN.
 * timeout_ms specifies the total wait budget; zero means no waiting.
 * The output frame is valid only when the function returns 0.
 *
 * Each matching frame is passed to the CAN analyzer and the OBD
 * activity/passive-data callback before being returned to the caller.
 *
 * Returns:
 *   0          A matching frame was received.
 *   EAGAIN     CAN is disabled, or no matching frame was returned
 *              by a nonblocking receive.
 *   ETIMEDOUT  No matching frame was received before timeout.
 *   EINVAL     The output pointer is NULL or timeout_ms is negative.
 *   ENODEV     The TWAI node or receive queue is unavailable.
 *   EIO        CAN initialization or configuration has failed.
 *
 * Queue waits have FreeRTOS tick resolution. Scheduling may delay
 * the return beyond the requested timeout.
 */
extern int can_receive(can_frame_t *, int);

/*
 * Encodes OBD packet to CAN frame and sends the resulting packet to the bus.
 *
 * Returns:
 *        0 - packet scheduled for transmission
 *   EBUSY  - currently busy with sending previous packet
 */
extern int can_obd_send(unsigned char *, int);

/*
 * Receive and reassemble one complete OBD packet synchronously.
 * Called from task context with exclusive access to CAN.
 * Uses the response timeout started by can_obd_send().
 * The returned buffer remains valid until the next receive call.
 *
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
 *   0   - A complete OBD packet was received
 *   EIO - The response timeout expired, CAN reception or Flow Control
 *         transmission failed, or the received packet was malformed.
 */
extern int can_obd_recv(unsigned char **, int *);

/*
 * Return TRUE while the OBD response window is active.
 * Background reception must not consume frames during this window.
 * Called from the same task as CAN and OBD processing.
 */
extern BOOL can_obd_rx_active(void);

#endif /* __CAN_HAL_H */

