/*********************************************************************
 *
 * Support for PCF8563 I2C real-time clock.
 *
 *********************************************************************/

#ifndef __PCF8563_H
#define __PCF8563_H

#include "GenericTypeDefs.h"

#include <sys/time.h>
#include <time.h>

/* public functions */
/* Initialization routine. */
extern int pcf8563_init(void);

/*
 * Get time and date.
 *
 * Return values:
 *       0 - operation finished, valid data available
 *  EAGAIN - reading in progress
 *  EFAULT - RTC contains invalid data - Low voltage indicator set
 */
extern int pcf8563_gettime(struct tm *);

/*
 * Set time and date.
 *
 * Return values:
 *       0 - operation finished, data have been written to the device
 *  EAGAIN - writing in progress
 */
extern int pcf8563_settime(struct tm *);

/* Displays last read values. */
extern void pcf8563_display_read_values(int (*)(const char *));

#endif /* __PCF8563_H */

