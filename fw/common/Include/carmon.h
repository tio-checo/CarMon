/*********************************************************************
 *
 * Definitions for CarMon service.
 *
 *********************************************************************/

#ifndef __CARMON_H
#define __CARMON_H

#include "GenericTypeDefs.h"

/* Carmon module commands */
#define	CM_CMD_CONF		'C'	/* configure the module */

/* types */
/* Operating modes. */
typedef enum _cm_mode_t {
	CM_MODE_CONFIGURE,
	CM_MODE_AUTONOMOUS,
	CM_MODE_ASSISTED
} cm_mode_t;

/* public functions */
/* Initialization routine. */
extern int cm_init(void);

/* Returns current operating mode. */
extern cm_mode_t cm_get_mode(void);

/* Processes query from master. */
extern int cm_process_query(char *, int);

#endif /* __CARMON_H */
