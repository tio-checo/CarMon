/*********************************************************************
 *
 * Software implementation of I2C bus.
 *
 *********************************************************************/

#ifndef __I2CSW_H
#define __I2CSW_H

#include "GenericTypeDefs.h"

/* constants */

/* types */

/* public functions */
/* Initialization routine. */
extern int i2csw_init(void);

/*
 * Returns TRUE if I2C bus is busy sending or receiving word.
 */
extern BOOL i2csw_i2c_bus_busy(void);

/*
 * Instruct lower layer to send out data on I2C bus.
 *
 * Parameters:
 *  data  - data to be sent out starting with MSB
 *  bits  - number of bits to be sent out
 *  start - if TRUE, then I2C start condition is generated
 *  stop  - if TRUE, then I2C stop condition is generated
 *
 * Returns: EBUSY if I2C bus is currently busy, otherwise 0.
 */
extern int i2csw_i2c_send(unsigned char, unsigned int, BOOL, BOOL);

/*
 * Instruct lower layer to receive data from I2C bus.
 *
 * Parameters:
 *  bits  - number of bits to be received
 *  pdata - pointer where to store received data
 *  ack   - if TRUE, then ACK is generated, otherwise NAK.
 *  stop  - if TRUE, then I2C stop condition is generated
 *
 * Returns: EAGAIN - receiving in progress
 *          0      - data available
 */
extern int i2csw_i2c_recv(unsigned char *, unsigned int, BOOL, BOOL);

/*
 * Handles I2C communication with I2CSW - lower layer.
 */
extern int i2csw_i2c_ll_svc(void);

#endif /* __I2CSW_H */

