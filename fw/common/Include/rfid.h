/*********************************************************************
 *
 * Support for RFID functions.
 *
 *********************************************************************/

#ifndef __RFID_H
#define __RFID_H

#include "GenericTypeDefs.h"

/* constants */
/* RFID module commands */
/* enable sending RFID messages to upper layer */
#define	MCOMM_RFID_CMD_SEND_ENABLE	'E'
/* disable sending RFID messages to upper layer */
#define	MCOMM_RFID_CMD_SEND_DISABLE	'D'
/* read logged records */
#define	MCOMM_RFID_CMD_LOG_READ		'R'
/* read the first record */
#define	MCOMM_RFID_CMD_LOG_READ_FIRST	'F'
/* read next record */
#define	MCOMM_RFID_CMD_LOG_READ_NEXT	'N'
/* delete logged records (clean up log file) */
#define	MCOMM_RFID_CMD_LOG_CLEAN	'C'

/* types */

/* public functions */
/* Initialization routine. */
extern int rfid_init(void);

/* Processes query from master targeted to RFID module */
extern int rfid_process_query(char *, int);

#endif /* __RFID_H */

