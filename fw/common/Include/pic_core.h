/*********************************************************************
 *
 * Support for PIC core functions.
 *
 *********************************************************************/

#ifndef __PIC_CORE_H
#define __PIC_CORE_H

#include "GenericTypeDefs.h"

/* constants */
/* PIC core module commands */
#define	MCOMM_PCORE_CMD_SETTIME	'D'	/* set time and date */
#define	MCOMM_PCORE_CMD_GETTIME	'd'	/* get time and date */
#define	MCOMM_PCORE_CMD_GETTEMP	't'	/* get temperature from MCP9801 */
#define	MCOMM_PCORE_CMD_COMCFG	'C'	/* configure communication parameters */
#define	MCOMM_PCORE_CMD_CALCCRC	'c'	/* calculate CRC-CCITT checksum */

/* types */

/* public functions */
/* Initialization routine. */
extern int pcore_init(void);

/* Processes query from master targeted to PIC core module */
extern int pcore_process_query(char *, int);

/* Reads date and time. Returns pointer to resulting string. */
extern char *pcore_gettime(void);

#endif /* __PIC_CORE_H */

