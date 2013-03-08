/*********************************************************************
 *
 * Handles OBD communication layer with ECU. This layer is independent
 * on real communication protocol used (KWP2000, CAN ...).
 *
 *********************************************************************/

#ifndef __OBD_H
#define __OBD_H

#include "GenericTypeDefs.h"

/* ECU module ID */
#define	MCOMM_ECU_ID		'E'

/* ECU submodule IDs */
#define	ECU_OBD_ID		'O'	/* OBD module */

/* OBD module commands */
#define	OBD_CMD_GETPID		'P'	/* get value of OBD PID */
#define	OBD_CMD_GETPIDS		'R'	/* get value of OBD PIDs periodically */

/* OBD modes */
#define	OBD_MODE_GET_PID	1	/* Show current data */
#define	OBD_MODE_GET_FF		2	/* Show freeze frame data */
#define	OBD_MODE_GET_DTC	3	/* Show Diagnostic Trouble Codes */
#define	OBD_MODE_CLR_DTC	4	/* Clear Diagnostic Trouble Codes */

#define	OBD_ECU_PACKET_MAXLN	256	/* Max length of ECU packet */

/* Initialization routine */
extern int obd_init(void);

/* Processes query from master targeted to ECU module */
extern int obd_process_query(char *, int);

/* service routine */
extern int obd_svc(void);
#endif /* __OBD_H */
