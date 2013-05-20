/*********************************************************************
 *
 * Support for MCP9801 temperature sensor.
 *
 *********************************************************************/

#ifndef __MCP9801_H
#define __MCP9801_H

#include "GenericTypeDefs.h"

/* constants */

/* types */

/* public functions */
/* Initialization routine. */
extern int mcp9801_init(void);
/* Get last measured temperature. */
extern int mcp9801_get_temperature(void);

#endif /* __MCP9801_H */

