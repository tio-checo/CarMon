#include <stdio.h>
/*********************************************************************
 *
 * Support for PCF8563 I2C real-time clock.
 *
 *********************************************************************/

#include <ctype.h>
#include <errno.h>
#include <sys/time.h>
#include <string.h>
#include <time.h>


#include "master_comm.h"
#include "i2c_hal.h"
#include "pcf8563.h"
#include "rt_light.h"

#include "driver/i2c_master.h"

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
#define PCF8563_STOP_MSK	(1 << PCF8563_CTRL1_STOP)

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
/* RTC device on the shared I2C bus. */
static i2c_master_dev_handle_t pcf8563_dev;

/* data buffer used for getting/setting time and date */
static unsigned char pcf8563_data[16 + 1];

/* local functions */

/* global functions */
/*
 * Initialization routine.
 * The shared I2C bus must already be initialized.
 */
int
pcf8563_init(void)
{
	i2c_master_bus_handle_t bus;
	i2c_device_config_t config = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = PCF8563_I2C_ADDRESS,
		.scl_speed_hz = 100000,
	};
	int err;

	/* Avoid registering the device more than once. */
	if (pcf8563_dev != NULL)
		return (0);

	/* Use the bus already initialized by i2c_init(). */
	err = i2c_master_get_bus_handle(I2C_NUM_0, &bus);
	if (err != ESP_OK)
		return (err);

	/* Verify that the RTC acknowledges its address. */
	err = i2c_master_probe(bus, PCF8563_I2C_ADDRESS, 100);
	if (err != ESP_OK)
		return (err);

	return (i2c_master_bus_add_device(bus, &config, &pcf8563_dev));
}

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
int
pcf8563_gettime(struct tm *currtm)
{
	struct tm newtm = { 0 };
	unsigned char address = PCF8563_REG_CTRL_STS_1;
	int err;

	if (currtm == NULL)
		return (EINVAL);

	if (pcf8563_dev == NULL)
		return (ENODEV);

	/*
	 * Read control registers and all time registers together.
	 * Wait for completion, with a timeout of 100 ms.
	 */
	err = i2c_master_transmit_receive(pcf8563_dev,
	    &address, sizeof (address), pcf8563_data, 9, 100);
	if (err != ESP_OK)
		return (err);

	/* Low voltage flag set signals unreliable RTC data. */
	if ((pcf8563_data[PCF8563_REG_CTRL_STS_1] & PCF8563_STOP_MSK) ||
	    (pcf8563_data[PCF8563_REG_VL_SECONDS] & PCF8563_VL_MSK)) {
		return (EFAULT);
	}

	/* Fill in time structure. */
	/* Seconds */
	newtm.tm_sec = BCD2DEC(pcf8563_data[PCF8563_REG_VL_SECONDS] & 0x7f);

	/* Minutes */
	newtm.tm_min = BCD2DEC(pcf8563_data[PCF8563_REG_MINUTES] & 0x7f);

	/* Hours */
	newtm.tm_hour = BCD2DEC(pcf8563_data[PCF8563_REG_HOURS] & 0x3f);

	/* Days */
	newtm.tm_mday = BCD2DEC(pcf8563_data[PCF8563_REG_DAYS] & 0x3f);

	/* Months */
	newtm.tm_mon = BCD2DEC(pcf8563_data[PCF8563_REG_CENT_MONTHS] &
	    0x1f) - 1;

	/* Years - preserve the existing 2000-2099 convention. */
	newtm.tm_year = BCD2DEC(pcf8563_data[PCF8563_REG_YEARS]) + 2000 - 1900;

	/* Publish the result only after a successful read. */
	*currtm = newtm;

	return (0);
}

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
int
pcf8563_settime(struct tm *newtm)
{
	unsigned char control[2] = { PCF8563_REG_CTRL_STS_1, 0 };
	int err;

	if (newtm == NULL)
		return (EINVAL);

	if (pcf8563_dev == NULL)
		return (ENODEV);

	/* Match the 2000-2099 convention used by pcf8563_gettime(). */
	if (newtm->tm_year < 100 || newtm->tm_year > 199)
		return (EINVAL);

	/* Populate message to be sent to RTC. */
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

	/* Write all time and date registers in one transaction. */
	err = i2c_master_transmit(pcf8563_dev, pcf8563_data, 8, 100);
	if (err != ESP_OK)
		return (err);

	/* Enable normal clock operation, clearing STOP and test bits. */
	return (i2c_master_transmit(pcf8563_dev, control,
	    sizeof (control), 100));
}

/*
 * Reads and displays RTC registers, including invalid clock states.
 * Must be called from task context.
 */
void
pcf8563_display_read_values(int (*print_str)(const char *))
{
	unsigned char address = PCF8563_REG_CTRL_STS_1;
	char s[81];
	int i, err;

	if (pcf8563_dev == NULL) {
		print_str("RTC driver not initialized\n\r");
		return;
	}

	/*
	 * Read registers directly so that VL and STOP do not prevent
	 * diagnostics from displaying the current hardware state.
	 */
	err = i2c_master_transmit_receive(pcf8563_dev,
	    &address, sizeof (address), pcf8563_data, 9, 100);
	if (err != ESP_OK) {
		snprintf(s, sizeof (s), "RTC read failed: %d\n\r", err);
		print_str(s);
		return;
	}

	/* header */
	print_str("-------------------------------------\n\r");

	for (i = 0; i < 9; i++) {
		snprintf(s, sizeof (s), "reg %02X: %02X\n\r",
		    (unsigned int)i, (unsigned int)pcf8563_data[i]);
		print_str(s);
	}

	snprintf(s, sizeof (s), "VL=%d, STOP=%d\n\r",
	    !!(pcf8563_data[PCF8563_REG_VL_SECONDS] & PCF8563_VL_MSK),
	    !!(pcf8563_data[PCF8563_REG_CTRL_STS_1] & PCF8563_STOP_MSK));
	print_str(s);

	/* footer */
	print_str("-------------------------------------\n\r");
}
