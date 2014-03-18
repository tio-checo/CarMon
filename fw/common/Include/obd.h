/*********************************************************************
 *
 * Handles OBD communication layer with ECU. This layer is independent
 * on real communication protocol used (KWP2000, CAN ...).
 *
 *********************************************************************/

#ifndef __OBD_H
#define __OBD_H

#include "GenericTypeDefs.h"
#include "CAN.h"

/* ECU submodule IDs */
#define	ECU_OBD_ID		'O'	/* OBD module */

/* OBD module commands */
#define	OBD_CMD_CONF		'C'	/* configure the module */
#define	OBD_CMD_GETPID		'P'	/* get value of OBD PID */
#define	OBD_CMD_GETPIDS		'R'	/* get value of OBD PIDs periodically */

/* OBD commands */
#define	OBD_MODE_GET_PID	1	/* Show current data */
#define	OBD_MODE_GET_FF		2	/* Show freeze frame data */
#define	OBD_MODE_GET_DTC	3	/* Show Diagnostic Trouble Codes */
#define	OBD_MODE_CLR_DTC	4	/* Clear Diagnostic Trouble Codes */

#define	OBD_ECU_PACKET_MAXLN	256	/* Max length of ECU packet */

/* Underlying communication layer. */
typedef enum {
    OBD_COMM_MODE_AUTO = 0,		/* Determined automatically */
    OBD_COMM_MODE_KWP2K_NORM,		/* KWP 2000 - normal mode */
    OBD_COMM_MODE_KWP2K_DEBUG,		/* KWP 2000 - debug mode */
    OBD_COMM_MODE_KWP2K_MIXED,		/* KWP 2000 mixed with CAN */
    OBD_COMM_MODE_CAN_SID_250,		/* CAN - 11bit ID, 250kbit */
    OBD_COMM_MODE_CAN_SID_500,		/* CAN - 11bit ID, 500kbit */
    OBD_COMM_MODE_CAN_EID_250,		/* CAN - 29bit ID, 250kbit */
    OBD_COMM_MODE_CAN_EID_500,		/* CAN - 29bit ID, 500kbit */
    OBD_COMM_MODE_NUMBER_OF_MODES
} OBD_COMM_MODE;


/* Initialization routine */
extern int obd_init(void);

/* Processes query from master targeted to ECU module */
extern int obd_process_query(char *, int);

/* Callback routine processing received CAN frames */
extern int obd_can_rx_cb(CANRxMessageBuffer *);

#endif /* __OBD_H */
