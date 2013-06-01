/*********************************************************************
 *
 * Support for PCF8563 I2C real-time clock.
 *
 *********************************************************************/

#include <ctype.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>

#include "HardwareProfile.h"
#include "xc.h"

#include "master_comm.h"
#include "i2c_sw.h"
#include "pcf8563.h"
#include "rt_light.h"

/* constants */
/* I2C address of PCF8563 chip */
#define PCF8563_I2C_ADDRESS	0x51

/* addresses of PCF8563 registers */
#define PCF8563_REG_CTRL_STS_1	0x0	/* control status 1 */
#define PCF8563_REG_CTRL_STS_2	0x1	/* control status 2 */
#define PCF8563_REG_VL_SECONDS	0x2	/* voltage low, seconds 00-59 in BCD */
#define PCF8563_REG_MINUTES	0x3	/* minutes 00-59 in BCD */
#define PCF8563_REG_HOURS	0x4	/* hours 00-23 in BCD */
#define PCF8563_REG_DAYS	0x5	/* days 01-31 in BCD */
#define PCF8563_REG_WEEKDAYS	0x6	/* weekdays 0-6 in BCD */
#define PCF8563_REG_CENT_MONTHS	0x7	/* century, months 01-12 in BCD */
#define PCF8563_REG_YEARS	0x8	/* years 00-99 in BCD */
#define PCF8563_REG_ALARM_MIN	0x9	/* minute alarm 00-59 in BCD */
#define PCF8563_REG_ALARM_HOUR	0xa	/* hour alarm 00-23 in BCD */
#define PCF8563_REG_ALARM_DAY	0xb	/* day alarm 01-31 in BCD */
#define PCF8563_REG_ALARM_WDAY	0xc	/* weekday alarm 0-6 in BCD */
#define PCF8563_REG_CLKOUT_CTRL	0xd	/* CLKOUT control */
#define PCF8563_REG_TIMER_CTRL	0xe	/* Timer control */
#define PCF8563_REG_TIMER	0xf	/* Timer */

/* Voltage low indicator bit and mask */
#define PCF8563_VL_BIT		7
#define PCF8563_VL_MSK		(1 << PCF8563_VL_BIT)
/* Century bit and mask */
#define PCF8563_CENTURY_BIT	7
#define PCF8563_CENTURY_MSK	(1 << PCF8563_CENTURY_BIT)
/* Alarm enable bit and mask - zero enables alarm (POR default is 1) */
#define PCF8563_AE_BIT		7
#define PCF8563_AE_MSK		(1 << PCF8563_AE_BIT)

/* Control status register 1 bits */
#define PCF8563_CTRL1_TEST1	7	/* 1 enables EXT_CLK test mode */
#define PCF8563_CTRL1_STOP	5	/* 1 stops RTC - flip-flops set to 0 */
#define PCF8563_CTRL1_TESTC	3	/* 1 enables power on reset override */

/* Control status register 2 bits */
#define PCF8563_CTRL2_TI_TP	4	/* see datasheet */
#define PCF8563_STS2_AF		3	/* alarm flag active */
#define PCF8563_STS2_TF		2	/* timer flag active */
#define PCF8563_CTRL2_AIE	1	/* alarm interrupt enabled */
#define PCF8563_CTRL2_TIE	0	/* timer interrupt enabled */

/* macros */
/* convert decimal value to BCD one */
#define DEC2BCD(x)		(((x) / 10) << 4 | ((x) % 10))
/* convert BCD value to decimal one */
#define BCD2DEC(x)		(((x) >> 4) * 10 + ((x) & 0xf))

/* local types */

/* local variables */
/* state machine handling communication with the sensor */
static enum {
	PCF8563_ST_IDLE = 0,	/* state machine idle */

	PCF8563_ST_GETTIME1,	/* Get time and date */
	PCF8563_ST_GETTIME2,
	PCF8563_ST_GETTIME3,

	PCF8563_ST_SETTIME1,	/* Set time and date */
	PCF8563_ST_SETTIME2
} pcf8563_st;

/* data buffer used for getting/setting time and date */
static unsigned char pcf8563_data[16 + 1];

/* local functions */

/*
 * Main service routine - called periodically from timer interrupt.
 * Handles communication with PCF8563.
 */
static int
pcf8563_svc(void)
{
	static unsigned char buf[1];

	switch (pcf8563_st) {
	/* service idle */
	case PCF8563_ST_IDLE:
		break;

	/*
	 * Get time and date - phase 1.
	 *
	 * Grab I2C bus.
	 */
	case PCF8563_ST_GETTIME1:
		if (i2csw_request_bus() != 0)
			break;

		/* Prepare data */
		buf[0] = PCF8563_REG_VL_SECONDS;

		pcf8563_st = PCF8563_ST_GETTIME2;
		break;

	/*
	 * Get time and date - phase 2
	 *
	 * Initialize address pointer.
	 */
	case PCF8563_ST_GETTIME2:
		if (i2csw_write(PCF8563_I2C_ADDRESS, buf, 1) == EAGAIN)
			break;

		pcf8563_st = PCF8563_ST_GETTIME3;
		break;

	/*
	 * Get time and date - phase 3
	 *
	 * Read time and date from RTC.
	 */
	case PCF8563_ST_GETTIME3:
		if (i2csw_read(PCF8563_I2C_ADDRESS, pcf8563_data, 7) == EAGAIN)
			break;

		/* Release I2C bus */
		i2csw_release_bus();

		pcf8563_st = PCF8563_ST_IDLE;
		break;

	/*
	 * Set time and date - phase 1
	 *
	 * Grab I2C bus.
	 */
	case PCF8563_ST_SETTIME1:
		if (i2csw_request_bus() != 0)
			break;

		pcf8563_st = PCF8563_ST_SETTIME2;
		break;

	/*
	 * Set time and date - phase 2
	 *
	 * Write RTC time and date registers. First byte in data buffer
	 * contains address of RTC register to be written first. RTC
	 * automatically increments address pointer after each byte is
	 * processed.
	 */
	case PCF8563_ST_SETTIME2:
		if (i2csw_write(PCF8563_I2C_ADDRESS, pcf8563_data, 8) == EAGAIN)
			break;

		/* Release I2C bus */
		i2csw_release_bus();

		pcf8563_st = PCF8563_ST_IDLE;
		break;

	} /* switch */

	return 0;
}

/*
 * Returns TRUE if the service is currently busy. Otherwise returns FALSE.
 */
static BOOL
pcf8563_svc_busy(void)
{
	return (pcf8563_st != PCF8563_ST_IDLE);
}

/* global functions */
/*
 * Initialization routine.
 */
int
pcf8563_init(void)
{
	/* Register and enable PCF8563 service. */
	rtl_svc_init("pcf8563", pcf8563_svc, TRUE, FALSE);

	/* State machine is IDLE */
	pcf8563_st = PCF8563_ST_IDLE;

	return 0;
}

/*
 * Get time and date.
 *
 * Return values:
 *       0 - operation finished, valid data available
 *  EAGAIN - reading in progress
 *  EFAULT - RTC contains invalid data - Low voltage indicator found set
 */
int
pcf8563_gettime(struct tm *currtm)
{
	static BOOL op_in_progress = FALSE;

	/* If operation is in progress, check if it finished. */
	if (op_in_progress) {
		/* Operation not finished yet, return. */
		if (pcf8563_svc_busy())
			return EAGAIN;

		/* Operation finished */
		op_in_progress = FALSE;

		/* Low voltage flag set signals corrupted RTC data, return */
		if (pcf8563_data[0] & PCF8563_VL_MSK)
			return EFAULT;

		/* Fill in time structure */
		/* Seconds */
		currtm->tm_sec = BCD2DEC(pcf8563_data[0] & 0x7f);

		/* Minutes */
		currtm->tm_min = BCD2DEC(pcf8563_data[1] & 0x7f);

		/* Hours */
		currtm->tm_hour = BCD2DEC(pcf8563_data[2] & 0x3f);

		/* Days */
		currtm->tm_mday = BCD2DEC(pcf8563_data[3] & 0x3f);

		/* Months */
		currtm->tm_mon = BCD2DEC(pcf8563_data[5] & 0x1f) - 1;

		/* Years */
		currtm->tm_year = BCD2DEC(pcf8563_data[6]) + 2000 - 1900;

		return 0;
	}

	/* Kick off getting process. */
	pcf8563_st = PCF8563_ST_GETTIME1;
	op_in_progress = TRUE;

	return EAGAIN;
}

/*
 * Set time and date.
 *
 * Return values:
 *       0 - operation finished, data have been written to the device
 *  EAGAIN - writing in progress
 */
int
pcf8563_settime(struct tm *newtm)
{
	static BOOL op_in_progress = FALSE;

	/* If operation is in progress, check if it finished. */
	if (op_in_progress) {
		/* Operation not finished yet, return. */
		if (pcf8563_svc_busy())
			return EAGAIN;

		/* Operation finished */
		op_in_progress = FALSE;

		return 0;
	}

	/* Setting time and date should be commenced. */
	/* Populate message to be sent to RTC */
	/* Do not validate provided data - caller is responsible for this. */

	/*
	 * First register address to be written. RTC increments address
	 * pointer automatically after each byte is written.
	 */
	pcf8563_data[0] = PCF8563_REG_VL_SECONDS;

	/* Seconds, clear VL flag */
	pcf8563_data[1] = DEC2BCD(newtm->tm_sec);

	/* Minutes */
	pcf8563_data[2] = DEC2BCD(newtm->tm_min);

	/* Hours */
	pcf8563_data[3] = DEC2BCD(newtm->tm_hour);

	/* Days */
	pcf8563_data[4] = DEC2BCD(newtm->tm_mday);

	/* Weekdays - not used for now */
	pcf8563_data[5] = 0;

	/* Months, clear century flag */
	pcf8563_data[6] = DEC2BCD(newtm->tm_mon + 1);

	/* Years */
	pcf8563_data[7] = DEC2BCD(newtm->tm_year % 100);

	/* Kick off setting process. */
	pcf8563_st = PCF8563_ST_SETTIME1;
	op_in_progress = TRUE;

	return EAGAIN;
}
