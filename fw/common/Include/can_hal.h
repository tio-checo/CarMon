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
/* configure the module  */
#define	MCOMM_CAN_CMD_CFG		'C'
/* get status of the module */
#define	MCOMM_CAN_CMD_GET_STATUS	's'
/* send packet */
#define	MCOMM_CAN_CMD_SEND_FRAME	'S'
/* packet received */
#define	MCOMM_CAN_RSP_FRAME_RECEIVED	'R'

/* types */
/* supported speeds */
typedef enum {
	SPEED_1M,
	SPEED_500k,
	SPEED_250k,
	SPEED_125k,
	CAN_NUM_SUPPORTED_SPEEDS
} CAN_SUPPORTED_SPEEDS;

/* public functions */
/* Initialization routine. */
extern int can_init(void);

/* Suspend the module. */
extern int can_suspend(void);

/* Processes query from master targeted to CAN module */
extern int can_process_query(char *, int);

#endif /* __CAN_HAL_H */

