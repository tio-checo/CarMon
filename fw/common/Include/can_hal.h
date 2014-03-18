/*********************************************************************
 *
 * Support for Controller Area Network (CAN) PIC module - provides
 * API for master device.
 *
 *********************************************************************/

#ifndef __CAN_HAL_H
#define __CAN_HAL_H

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
 * Configures the CAN module.
 * Returns:
 *   0      - speed configured.
 *   EINVAL - specified mode or speed not supported.
 *   EBUSY  - module currently busy, speed can't be set.
 */
extern int can_configure(int, int, int, UINT32, UINT32, BOOL);

/*
 * Checks if configuration process is in progress.
 * Returns:
 *   TRUE if configuration is in progress; FALSE otherwise.
 */
extern BOOL can_configuration_in_progress(void);

#endif /* __CAN_HAL_H */

