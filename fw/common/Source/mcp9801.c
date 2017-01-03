/*********************************************************************
 *
 * Support for MCP9801 I2C temperature sensor. Periodically reads ambient
 * temperature and passes it to the master on request.
 *
 *********************************************************************/

#include <ctype.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>

#include "HardwareProfile.h"
#include "xc.h"

#include "master_comm.h"
#include "i2c_hal.h"
#include "mcp9801.h"
#include "rt_light.h"

/* constants */
/* I2C address of MCP9801 chip */
#define MCP9801_I2C_ADDRESS	0x4F

/* addresses of MCP9801 registers */
#define MCP9801_REG_TA		0	/* ambient temperature register */
#define MCP9801_REG_CONF	1	/* configuration register */
#define MCP9801_REG_THYST	2	/* temperature hysteresis register */
#define MCP9801_REG_TSET	3	/* temperature limit-set register */

/* 500 ms temperature acquisition period. */
#define MCP9801_ACQ_PERIOD	500000UL

/* local types */

/* local variables */
/* state machine handling communication with the sensor */
static enum {
	MCP9801_ST_IDLE = 0,	/* state machine idle */
	MCP9801_ST_INIT1,	/* Initialize MCP9801 chip */
	MCP9801_ST_INIT2,
	MCP9801_ST_INIT3,

	MCP9801_ST_GETTEMP1,	/* Get measured temperature */
	MCP9801_ST_GETTEMP2,
	MCP9801_ST_GETTEMP3
} mcp9801_st;

/* measured temperature */
static unsigned int mcp9801_temperature;

/* measures temperature acquisition period */
static rtl_timer_t *mcp9801_acq_timer;

/* local functions */

/*
 * Main service routine - called periodically from timer interrupt.
 * Handles communication with MCP9801.
 */
static int
mcp9801_svc(void)
{
	static unsigned char buf[2];

	switch (mcp9801_st) {
	/* service idle */
	case MCP9801_ST_IDLE:
		break;

	/*
	 * MCP9801 chip initialization - phase 1
	 *
	 * Grab I2C bus.
	 */
	case MCP9801_ST_INIT1:
		if (i2c_request_bus() != 0)
			break;

		/* Prepare data */
		buf[0] = MCP9801_REG_CONF;
		buf[1] = 0x60;

		mcp9801_st = MCP9801_ST_INIT2;
		break;

	/*
	 * MCP9801 chip initialization - phase 2
	 *
	 * Write MCP9801 configuration register:
	 *  - one-shot disabled (default)
	 *  - 12-bit ADC resolution (default is 9-bit)
	 *  - 1 fault queue bit (default)
	 *  - active-low alert (default)
	 *  - comparator mode (default)
	 *  - shutdown disable (default)
	 */
	case MCP9801_ST_INIT2:
		if (i2c_write(MCP9801_I2C_ADDRESS, buf, 2) == EAGAIN)
			break;

		/* Prepare data */
		buf[0] = MCP9801_REG_TA;

		mcp9801_st = MCP9801_ST_INIT3;
		break;

	/*
	 * MCP9801 chip initialization - phase 3
	 *
	 * Set MCP9801 register pointer to address of temperature register,
	 * so that subsequent reads from the chip return measured temperature.
	 */
	case MCP9801_ST_INIT3:
		if (i2c_write(MCP9801_I2C_ADDRESS, buf, 1) == EAGAIN)
			break;

		/* Release I2C bus */
		i2c_release_bus();

		/* Init timer measuring acquisition period. */
		rtl_timer_init(&mcp9801_acq_timer, MCP9801_ACQ_PERIOD);

		/* Proceed with periodic reading of temperature. */
		mcp9801_st = MCP9801_ST_GETTEMP1;
		break;

	/*
	 * Get measured temperature.
	 *
	 * Grab I2C bus.
	 */
	case MCP9801_ST_GETTEMP1:
		if (i2c_request_bus() != 0)
			break;

		mcp9801_st = MCP9801_ST_GETTEMP2;
		break;

	/*
	 * Get measured temperature.
	 *
	 * Read 2 bytes from the sensor.
	 */
	case MCP9801_ST_GETTEMP2:
		if (i2c_current_read(MCP9801_I2C_ADDRESS, buf, 2) == EAGAIN)
			break;

		/* Move obtained temperature to resulting buffer. */
		mcp9801_temperature = buf[0] << 8 | buf[1];

		/* Release I2C bus */
		i2c_release_bus();

		mcp9801_st = MCP9801_ST_GETTEMP3;
		break;

	/* Wait for a while, then start another acquisition cycle. */
	case MCP9801_ST_GETTEMP3:
		if (rtl_timer_expired(mcp9801_acq_timer)) {
			/* Init timer measuring acquisition period. */
			rtl_timer_init(&mcp9801_acq_timer, MCP9801_ACQ_PERIOD);

			mcp9801_st = MCP9801_ST_GETTEMP1;
		}

		break;
	} /* switch */

	return 0;
}

/* global functions */
/*
 * Initialization routine.
 */
int
mcp9801_init(void)
{
	mcp9801_temperature = 0x55aa;

	/* Initialize timer measuring acquisition period. */
	mcp9801_acq_timer = NULL;
	rtl_timer_init(&mcp9801_acq_timer, 0);

	/* Register and enable MCP9801 service. */
	rtl_svc_init("mcp9801", mcp9801_svc, TRUE, FALSE);

	/* Initiate periodic reading from the sensor */
	mcp9801_st = MCP9801_ST_INIT1;

	return 0;
}

/*
 * Get last measured temperature.
 */
int
mcp9801_get_temperature(void)
{
	return (mcp9801_temperature);
}
