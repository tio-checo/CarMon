/*********************************************************************
 *
 * Hardware independent I2C bus API.
 *
 *********************************************************************/

#ifndef __I2CHAL_H
#define __I2CHAL_H

#include "GenericTypeDefs.h"

/* constants */

/* types */
/* I2C message */
typedef struct i2c_msg_s {
	unsigned char dev_addr;	/* device address */
	unsigned char data_addr;	/* data address */
	unsigned char *buf;	/* pointer to data buffer */
	int ln;		/* # of bytes to be read or written */
} i2c_msg_t;

/* public functions */
/* Initialization routine. */
extern int i2c_init(void);

/*
 * Grants I2C bus to a consumer. Must be called by consumer before
 * communication with I2C device is commenced. That assures that
 * only one device accesses I2C bus at a time.
 *
 * Returns 0 if bus was granted, otherwise returns EBUSY.
 */
extern int i2c_request_bus(void);

/*
 * Releases I2C bus. Must be called by consumer after the consumer
 * is done with communication.
 *
 * Returns 0.
 */
extern int i2c_release_bus(void);

/*
 * Writes data to I2C device.
 *
 * It is a non-blocking function to be called periodically until
 * communication layer finishes writing.
 *
 * Parameters:
 *  addr - I2C address of the device
 *  data - pointer to data buffer
 *  ln   - number of bytes to be read from the device
 *
 * Return values:
 *  0      - Writing finished
 *  EPERM  - Bus not granted to the consumer
 *  ENXIO  - No device responded to specified address
 *  EIO    - Communication error
 *  EAGAIN - Writing in progress
 */
extern int i2c_write(unsigned char, unsigned char *, int);

/*
 * Reads data sequentially from I2C device starting from current address.
 * It is a non-blocking function to be called periodically until
 * communication layer finishes reading.
 *
 * Parameters:
 *  addr - I2C address of the device
 *  data - pointer to data buffer
 *  ln   - number of bytes to be read from the device
 *
 * Return values:
 *  0      - reading finished, buffer contains valid data
 *  EPERM  - Bus not granted to the consumer
 *  ENXIO  - No device responded to specified address
 *  EIO    - Communication error
 *  EAGAIN - reading now in progress
 */
extern int i2c_current_read(unsigned char, unsigned char *, int);

/*
 * Reads data sequentially from I2C device starting from given address.
 * It is a non-blocking function to be called periodically until
 * communication layer finishes reading.
 *
 * Parameters:
 *  addr - I2C address of the device
 *  data_addr - data address
 *  data - pointer to data buffer
 *  ln   - number of bytes to be read from the device
 *
 * Return values:
 *  0      - reading finished, buffer contains valid data
 *  EPERM  - Bus not granted to the consumer
 *  ENXIO  - No device responded to specified address
 *  EIO    - Communication error
 *  EAGAIN - reading now in progress
 */
extern int i2c_random_read(unsigned char, unsigned char, unsigned char *,
    int);
#endif /* __I2CHAL_H */

