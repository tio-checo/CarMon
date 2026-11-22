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
 * Get time and date synchronously. Must be called from task context.
 *
 * Return values:
 *       0 - operation finished, data available
 *  EINVAL - invalid output pointer
 *  ENODEV - RTC driver not initialized
 *  EFAULT - RTC voltage low flag set or clock stopped
 *
 * I2C errors are returned unchanged as ESP-IDF error codes.
 */
extern int pcf8563_gettime(struct tm *);

/*
 * Set time and date synchronously. Must be called from task context.
 * The caller is responsible for validating the calendar date.
 *
 * Return values:
 *       0 - operation finished, data written to the device
 *  EINVAL - invalid input pointer or unsupported year
 *  ENODEV - RTC driver not initialized
 *
 * I2C errors are returned unchanged as ESP-IDF error codes.
 */
extern int pcf8563_settime(struct tm *);

/* Reads and displays RTC registers. Must be called from task context. */
extern void pcf8563_display_read_values(int (*)(const char *));

#endif /* __PCF8563_H */

