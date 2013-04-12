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
#define	MCOMM_PCORE_CMD_SETTIME	'T'	/* set time and date */
#define	MCOMM_PCORE_CMD_GETTIME	'D'	/* get time and date */

/* types */

/* public functions */
/* Initialization routine. */
extern int pcore_init(void);

/* Processes query from master targeted to PIC core module */
extern int pcore_process_query(char *, int);

/* Main acquisition service routine - called periodically from timer interrupt. */
extern int pcore_svc(void);

#endif /* __PIC_CORE_H */

