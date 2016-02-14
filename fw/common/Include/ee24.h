/*********************************************************************
 *
 * Support for 24Cxx I2C EEPROM.
 *
 *********************************************************************/

#ifndef __EE24_H
#define __EE24_H

#include "GenericTypeDefs.h"

#include <sys/time.h>
#include <time.h>

/* public functions */
/* Initialization routine. */
extern int ee24_init(void);

/*
 * Gets EEPROM parameter.
 *
 * Return values:
 *       0 - operation finished, valid data available
 *  EINVAL - invalid parameter number
 *           or invalid EEPROM content
 */
extern int ee24_getpar(int, int *, unsigned char **);

/*
 * Sets EEPROM parameter.
 * Data provided in form of HEX ASCII string.
 *
 * Return values:
 *       0 - operation finished, parameter stored to EEPROM
 *  EINVAL - invalid parameter number
 *  EAGAIN - writing in progress
 */
extern int ee24_setpar(int, int, char *);

/* Returns TRUE if EEPROM content is valid; FALSE otherwise. */
extern BOOL ee24_params_valid(void);

/* Returns TRUE if EEPROM parameters are copied to memory; FALSE otherwise. */
extern BOOL ee24_eepars_initialized(void);

/* Prints diagnostic information. */
extern void ee24_print_diag_info(int (*)(const char *));

#endif /* __EE24_H */

