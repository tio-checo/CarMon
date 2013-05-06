/*********************************************************************
 *
 * Logging service.
 *
 *********************************************************************/

#ifndef __LOG_H
#define __LOG_H

#include "GenericTypeDefs.h"

/* constants */

/* types */

/* public functions */
/* Initialization routine. */
extern int log_init(void);

/* Logs an event. */
extern int log_post(char *);

/* Read record from log device. */
extern int log_get_record(BOOL, char **, int);

/** Clean up log file. */
extern int log_clean(void);

/* Returns TRUE if logging service is currently busy. */
extern BOOL log_busy(void);

/* Upper layer logging service. */
extern int log_svc(void);

#endif /* __LOG_H */

