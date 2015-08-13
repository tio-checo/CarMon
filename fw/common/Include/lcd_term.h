/*********************************************************************
 *
 * Support for LCD terminal connected via I2C bus.
 *
 *********************************************************************/

#ifndef __LCDTERM_H
#define __LCDTERM_H

#include "GenericTypeDefs.h"

#include <sys/time.h>
#include <time.h>

/* public functions */
/* Initialization routine. */
extern int lcdterm_init(void);

/*
 * Returns information about framebuffer.
 *
 * Return values:
 *       0 - success
 */
extern int lcdterm_fb_info(char **, int *);

/*
 * Transfers framebuffer to the terminal.
 *
 * Return values:
 *      0 - operation successfully commenced
 *  EBUSY - module currently busy
 */
extern int lcdterm_fb_write(void);

#endif /* __LCDTERM_H */

