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

/* Periodic service routine, called from CAN task. */
extern int can_svc(void);

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
 * Receive one CAN frame without waiting.
 * Called from task context with exclusive access to CAN.
 * The output frame is valid only when the function returns 0.
 *
 * Returns:
 *   0       A matching frame was received.
 *   EAGAIN  CAN is disabled, the queue is empty, or the received
 *           frame does not match the requested SID/EID selection.
 *   EINVAL  The output pointer is NULL.
 *   ENODEV  The TWAI node or receive queue is unavailable.
 *   EIO     CAN initialization or configuration has failed.
 */
extern int can_receive(can_frame_t *);

/*
 * Encodes OBD packet to CAN frame and sends the resulting packet to the bus.
 *
 * Returns:
 *        0 - packet scheduled for transmission
 *   EBUSY  - currently busy with sending previous packet
 */
extern int can_obd_send(unsigned char *, int);

/*
 * Waits for CAN frame carrying OBD packet.
 *
 * Returns:
 *        0 - packet received
 *   EAGAIN - operation in progress
 *   EIO    - ECU did not respond
 */
extern int can_obd_recv(unsigned char **, int *);

#endif /* __CAN_HAL_H */

