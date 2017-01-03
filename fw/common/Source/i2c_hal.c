/*********************************************************************
 *
 * Hardware independent I2C bus API.
 *
 *********************************************************************/

#include <errno.h>

#include "HardwareProfile.h"
#include "xc.h"

#ifdef I2CSW_MODULE
#include "i2c_sw.h"
#else
#error "No I2C low level driver avialable."
#endif
#include "i2c_hal.h"

#include "rt_light.h"

/* local types */

/* local variables */
/* TRUE if I2C bus is currently granted to consumer. */
static BOOL i2c_bus_granted;

/* I2C message */
static i2c_msg_t i2c_msg;

/* state machine handling communication - upper layer */
static enum {
	I2C_UL_ST_IDLE = 0,	/* state machine idle */
	I2C_UL_ST_WRITE1,		/* Write bytes to device */
	I2C_UL_ST_WRITE2,
	I2C_UL_ST_WRITE3,

	I2C_UL_ST_RANDOM_READ1,	/* Read bytes from given address */
	I2C_UL_ST_RANDOM_READ2,
	I2C_UL_ST_CURRENT_READ1,	/* Read bytes from current address */
	I2C_UL_ST_CURRENT_READ2
} i2c_ul_st;

/* local functions */
/*
 * Returns TRUE if underlying I2C bus is busy sending or receiving word.
 */
static BOOL
i2c_bus_busy(void)
{
#ifdef I2CSW_MODULE
	return (i2csw_i2c_bus_busy());
#endif
}

/*
 * Returns TRUE if I2C upper layer is busy reading from or writing to device.
 */
static BOOL
i2c_ul_busy(void)
{
	return (i2c_ul_st != I2C_UL_ST_IDLE);
}

/*
 * Handles I2C communication - upper layer.
 */
static int
i2c_ul_svc(void)
{
	static unsigned int cnt;

	switch (i2c_ul_st) {
	/* service idle */
	case I2C_UL_ST_IDLE:
		break;

	/*
	 * Write to device - phase 1
	 *
	 * Address I2C device - RW bit is zero (write operation).
	 * Let lower layer generate I2C start condition.
	 */
	case I2C_UL_ST_WRITE1:
		/* Make sure that I2C bus is idle */
		if (i2c_bus_busy())
			break;

#ifdef I2CSW_MODULE
		i2csw_i2c_send((i2c_msg.dev_addr << 1) | 0, 8, TRUE,
		    FALSE);
#endif

		/* Initialize byte counter. */
		cnt = 0;

		i2c_ul_st = I2C_UL_ST_WRITE2;
		break;

	/*
	 * Write to device - phase 2
	 *
	 * Write data byte to the device.
	 */
	case I2C_UL_ST_WRITE2: {
		BOOL last;

		/* Make sure that I2C bus is idle */
		if (i2c_bus_busy())
			break;

		last = (cnt + 1 == i2c_msg.ln);

		/* If it is the last byte, generate stop condition. */
#ifdef I2CSW_MODULE
		i2csw_i2c_send(i2c_msg.buf[cnt], 8, FALSE, last);
#endif

		/* Increment byte counter - used as index to data buffer */
		cnt++;

		/* All bytes written. */
		if (last) {
			i2c_ul_st = I2C_UL_ST_WRITE3;
		}
	} break;

	/*
	 * Write to device - phase 3
	 *
	 * Wait until lower layer finishes its job.
	 */
	case I2C_UL_ST_WRITE3:
		/* Wait until I2C bus is idle */
		if (i2c_bus_busy())
			break;

		i2c_ul_st = I2C_UL_ST_IDLE;
		break;

	/*
	 * Read bytes from random address - phase 1.
	 *
	 * Address the device - RW bit is zero, since
	 * next data address is to be written.
	 * Let lower layer generate I2C start condition.
	 */
	case I2C_UL_ST_RANDOM_READ1:
		/* Make sure that I2C bus is idle */
		if (i2c_bus_busy())
			break;

#ifdef I2CSW_MODULE
		i2csw_i2c_send((i2c_msg.dev_addr << 1) | 0, 8, TRUE,
		    FALSE);
#endif

		i2c_ul_st = I2C_UL_ST_RANDOM_READ2;
		break;

	/*
	 * Read bytes from random address - phase 2.
	 *
	 * Write data address to the device.
	 * Then proceed with "current read" operation.
	 */
	case I2C_UL_ST_RANDOM_READ2:
		/* Make sure that I2C bus is idle */
		if (i2c_bus_busy())
			break;

#ifdef I2CSW_MODULE
		i2csw_i2c_send(i2c_msg.data_addr, 8, FALSE, FALSE);
#endif

		i2c_ul_st = I2C_UL_ST_CURRENT_READ1;
		break;

	/*
	 * Read bytes from current address - phase 1.
	 *
	 * Address the device - RW bit is one (read operation).
	 * Let lower layer generate I2C start condition.
	 */
	case I2C_UL_ST_CURRENT_READ1:
		/* Make sure that I2C bus is idle */
		if (i2c_bus_busy())
			break;

#ifdef I2CSW_MODULE
		i2csw_i2c_send((i2c_msg.dev_addr << 1) | 1, 8, TRUE,
		    FALSE);
#endif

		/* Initialize byte counter. */
		cnt = 0;

		i2c_ul_st = I2C_UL_ST_CURRENT_READ2;
		break;

	/* Read one byte from the device. */
	case I2C_UL_ST_CURRENT_READ2: {
		BOOL last = (cnt + 1 == i2c_msg.ln);

		/*
		 * Wait until byte is received.
		 *
		 * If this is the last byte to be received,
		 * instruct lower layer to generate NAK and stop condition.
		 * Otherwise instruct lower layer to generate ACK which
		 * indicates to device that another byte is to be received.
		 */
#ifdef I2CSW_MODULE
		if (i2csw_i2c_recv(&i2c_msg.buf[cnt], 8, !last, last) != 0)
			break;
#endif

		/* Increment byte counter - used as index to data buffer */
		cnt++;

		/* All bytes read, go to idle state. */
		if (last) {
			i2c_ul_st = I2C_UL_ST_IDLE;
		}

	} break;
	} /* switch */

	return 0;
}

/*
 * Main service routine - called periodically from timer interrupt.
 */
static int
i2c_svc(void)
{
	/* Handle communication with I2C - lower layer */
#ifdef I2CSW_MODULE
	i2csw_i2c_ll_svc();
#endif

	/* Handle communication with I2C - upper layer */
	i2c_ul_svc();

	return 0;
}

/* global functions */
/*
 * Initialization routine.
 */
int
i2c_init(void)
{
#ifdef I2CSW_MODULE
	i2csw_init();
#endif

	/* Initialize I2C communication state machines. */
	/* Upper layer is idle. */
	i2c_ul_st = I2C_UL_ST_IDLE;

	/* Bus is available. */
	i2c_bus_granted = FALSE;

	/* Register and enable I2C service. */
	rtl_svc_init("i2c", i2c_svc, TRUE, FALSE);

	return 0;
}

/*
 * Grants I2C bus to a consumer. Must be called by consumer before
 * communication with I2C device is commenced. That assures that
 * only one device accesses I2C bus at a time.
 *
 * Returns 0 if bus was granted, otherwise returns EBUSY.
 */
int
i2c_request_bus(void)
{
	if (i2c_bus_granted)
		return EBUSY;

	i2c_bus_granted = TRUE;

	return 0;
}

/*
 * Releases I2C bus. Must be called by consumer after the consumer
 * is done with communication.
 *
 * Returns 0.
 */
int
i2c_release_bus(void)
{
	i2c_bus_granted = FALSE;

	return 0;
}

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
int
i2c_write(unsigned char addr, unsigned char *data, int ln)
{
	static BOOL write_in_progress = FALSE;

	/* Bus is not granted, do not commence an operation. */
	if (!i2c_bus_granted)
		return EPERM;

	/* If operation is in progress, check if it finished. */
	if (write_in_progress) {
		/* Operation not finished yet, return. */
		if (i2c_ul_busy())
			return EAGAIN;

		/* Operation finished */
		write_in_progress = FALSE;

		return 0;
	}

	/* New writing should be commenced. */
	/* It is no-op if there are no bytes to be written. */
	if (ln <= 0)
		return 0;

	/* Prepare I2C message */
	i2c_msg.dev_addr = addr;
	i2c_msg.buf = data;
	i2c_msg.ln = ln;

	/* Kick off writing process. */
	i2c_ul_st = I2C_UL_ST_WRITE1;
	write_in_progress = TRUE;

	return EAGAIN;
}

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
int
i2c_current_read(unsigned char addr, unsigned char *data, int ln)
{
	static BOOL read_in_progress = FALSE;

	/* Bus is not granted, do not commence an operation. */
	if (!i2c_bus_granted)
		return EPERM;

	/* If operation is in progress, check if it finished. */
	if (read_in_progress) {
		/* Operation not finished yet, return. */
		if (i2c_ul_busy())
			return EAGAIN;

		/* Operation finished */
		read_in_progress = FALSE;

		return 0;
	}

	/* New reading should be commenced. */
	/* It is no-op if there are no bytes to be read. */
	if (ln <= 0)
		return 0;

	/* Prepare I2C message */
	i2c_msg.dev_addr = addr;
	i2c_msg.buf = data;
	i2c_msg.ln = ln;

	/* Kick off reading process. */
	i2c_ul_st = I2C_UL_ST_CURRENT_READ1;
	read_in_progress = TRUE;

	return EAGAIN;
}

/*
 * Reads data sequentially from I2C device starting from given address.
 * It is a non-blocking function to be called periodically until
 * communication layer finishes reading.
 *
 * master: <S><DA><W> <BA> <S><DA><R>     <A>  <A> ...    <N><P>
 * slave:           <A>  <A>        <A><D1> <D2>   ... <Dn>
 *
 * where
 *
 * <S>  - start condition
 * <P>  - stop condition
 * <A>  - ACK
 * <N>  - NAK
 * <W>  - R/W bit set to 0 (write operation)
 * <R>  - R/W bit set to 1 (read operation)
 * <DA> - 7bit device I2C address
 * <BA> - data (byte) address
 * <Dx> - data
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
int
i2c_random_read(unsigned char addr, unsigned char data_addr,
    unsigned char *data, int ln)
{
	static BOOL read_in_progress = FALSE;

	/* Bus is not granted, do not commence an operation. */
	if (!i2c_bus_granted) {
		return EPERM;
	}

	/* If operation is in progress, check if it finished. */
	if (read_in_progress) {
		/* Operation not finished yet, return. */
		if (i2c_ul_busy())
			return EAGAIN;

		/* Operation finished */
		read_in_progress = FALSE;

		return 0;
	}

	/* New reading should be commenced. */
	/* It is no-op if there are no bytes to be read. */
	if (ln <= 0) {
		return 0;
	}

	/* Prepare I2C message */
	i2c_msg.dev_addr = addr;
	i2c_msg.data_addr = data_addr;
	i2c_msg.buf = data;
	i2c_msg.ln = ln;

	/* Kick off reading process. */
	i2c_ul_st = I2C_UL_ST_RANDOM_READ1;
	read_in_progress = TRUE;

	return EAGAIN;
}
