/*********************************************************************
 *
 * Support for LCD terminal connected via I2C bus.
 *
 *********************************************************************/

#include <errno.h>

#include "HardwareProfile.h"
#include "xc.h"

#include "i2c_hal.h"
#include "lcd_term.h"
#include "rt_light.h"

/* constants */
/* I2C address of LCD terminal */
#define LCDTERM_I2C_ADDRESS	0x31

/* addresses of terminal registers */
#define LCDTERM_REG_CTRL_STS_1	0x0	/* control status 1 */
#define LCDTERM_REG_CTRL_STS_2	0x1	/* control status 2 */
/* addresses 0x2 - 0x0f are reserved for future use */
#define LCDTERM_REG_FB		0x10	/* framebuffer */

/* MAX44009 Ambient Light Sensor */
/* I2C ALS address */
#define ALS_I2C_ADDRESS		0x4a

/* ALS registers */
#define ALS_REG_INTS		0x00	/* Interrupt Status */
#define ALS_REG_INTE		0x01	/* Interrupt Enable */
#define ALS_REG_CFG		0x02	/* Configuration */
#define ALS_REG_LUX_HIGH	0x03	/* Lux High Byte */
#define ALS_REG_LUX_LOW		0x04	/* Lux Low Byte */
#define ALS_REG_THR_UPPER	0x05	/* Upper Threshold High Byte */
#define ALS_REG_THR_LOWER	0x06	/* Lower Threshold High Byte */
#define ALS_REG_THR_TIMER	0x07	/* Threshold Timer */

/* size of LCD frame buffer */
#define LCDTERM_FB_SIZE		16

/* 5 second initial wait after power up. */
#define LCDTERM_INIT_WAIT	5000000UL

/* Frame buffer refresh rate set to 1 second. */
#define LCDTERM_FB_REFRESH_RATE	1000000UL

/* macros */

/* local types */

/* local variables */
/* state machine handling communication with the terminal */
static enum {
	LCDTERM_ST_IDLE = 0,	/* state machine idle */

	LCDTERM_ST_WAIT,	/* Wait for a while  */

	LCDTERM_ST_ALS1,	/* Read ALS and calculate LCD backlight */
	LCDTERM_ST_ALS2,

	LCDTERM_ST_SENDFB1,	/* Write framebuffer */
	LCDTERM_ST_SENDFB2
} lcdterm_st;

/* Frame buffer */
static char lcdterm_fb[LCDTERM_FB_SIZE];
/*
 * I2C data buffer
 * 1st character is reserved for FB I2C register address,
 * followed by frame buffer itself, then followed by one
 * byte used to transfer calculated LCD backlight value.
 */
static char lcdterm_i2c_buf[1 + LCDTERM_FB_SIZE + 1];
/* Calculated LCD backlight */
static unsigned char lcdterm_backlight;

/* Timer controlling frame buffer refresh rate */
static rtl_timer_t *lcdterm_timer;

/* local functions */

/*
 * Main service routine - called periodically from timer interrupt.
 * Handles communication with LCDTERM.
 */
static int
lcdterm_svc(void)
{
	int i, j, ali;

	switch (lcdterm_st) {
	/* service idle */
	case LCDTERM_ST_IDLE:
		break;

	/*
	 * Wait for a while after power up
	 * and between frame buffer refreshes.
	 */
	case LCDTERM_ST_WAIT:
		if (rtl_timer_expired(lcdterm_timer)) {
			rtl_timer_init(&lcdterm_timer, LCDTERM_FB_REFRESH_RATE);

			lcdterm_st = LCDTERM_ST_ALS1;
		}

		break;

	/*
	 * Read Ambient Light Sensor.
	 *
	 * Grab I2C bus.
	 */
	case LCDTERM_ST_ALS1:
		if (i2c_request_bus() == 0) {
			lcdterm_st = LCDTERM_ST_ALS2;
		}
		break;

	/*
	 * Read Ambient Light Sensor.
	 *
	 * Get value from the device and use it to calculate LCD backlight.
	 */
	case LCDTERM_ST_ALS2:
		if (i2c_random_read(ALS_I2C_ADDRESS,
		    ALS_REG_LUX_HIGH, &lcdterm_backlight, 1) == EAGAIN) {
			break;
		}

		/*
		 * Calculate light intensity from obtained raw value -
		 * upper half of obtained byte contains exponent,
		 * lower one four most significant bits of mantissa.
		 * The remaining lower four bits of mantissa are
		 * in the Lux Low Byte Register.
		 * Neglect it, since we are not after resolution.
		 *
		 * absolute light intensity = 0.72 * RAW[3:0] * 2^RAW[7:4]
		 *
		 */
		/* Calculate LOG2 for mantissa */
		for (i = 3, j = 0x08; i > 0; i--, j >>= 1) {
			if (lcdterm_backlight & j) {
				break;
			}
		}

		ali = (lcdterm_backlight >> 4) + i - 5;

		if (ali < 0) {
			lcdterm_backlight = 0x01;
		} else if (ali > 7) {
			lcdterm_backlight = 0xff;
		} else {
			lcdterm_backlight = 1 << ali;
		}

		lcdterm_st = LCDTERM_ST_SENDFB1;
		
		break;

	/*
	 * Send framebuffer to the terminal - phase 1.
	 *
	 * Grab I2C bus.
	 */
	case LCDTERM_ST_SENDFB1:
		/* Prepare data */
		lcdterm_i2c_buf[0] = LCDTERM_REG_FB;

		/*
		 * Copy framebuffer to shadow data buffer
		 * to prevent data inconsistencies.
		 */
		for (i = 0; i < LCDTERM_FB_SIZE; i++) {
			lcdterm_i2c_buf[i + 1] = lcdterm_fb[i];
		}

		/* LCD backlight. */
		lcdterm_i2c_buf[sizeof (lcdterm_i2c_buf) - 1] =
		    lcdterm_backlight;

		lcdterm_st = LCDTERM_ST_SENDFB2;

		break;

	/*
	 * Send framebuffer to the terminal - phase 2.
	 *
	 * Send framebuffer to the terminal.
	 * First byte of data buffer contains address of framebuffer register.
	 * Last byte of data buffer contains value of calculated LCD backlight.
	 * The terminal automatically increments address pointer after
	 * each byte is received.
	 */
	case LCDTERM_ST_SENDFB2:
		if (i2c_write(LCDTERM_I2C_ADDRESS,
		    (unsigned char *) lcdterm_i2c_buf,
		    sizeof (lcdterm_i2c_buf)) == EAGAIN)
			break;

		/* Release I2C bus */
		i2c_release_bus();

		lcdterm_st = LCDTERM_ST_WAIT;
		break;

	} /* switch */

	return 0;
}

/* global functions */
/*
 * Initialization routine.
 */
int
lcdterm_init(void)
{
	/* Register and enable LCDTERM service. */
	rtl_svc_init("lcdterm", lcdterm_svc, TRUE, FALSE);

	/* Initialize LCD backlight value to minimum. */
	lcdterm_backlight = 0x01;

	/* Initialize timer controlling frame buffer refresh rate. */
	rtl_timer_init(&lcdterm_timer, LCDTERM_INIT_WAIT);

	/* Initialize frame buffer to something readable. */
	snprintf(lcdterm_fb, sizeof (lcdterm_fb), " CarMon 2017(c)");
	lcdterm_fb[15] = ' ';

	/* Start state machine. */
	lcdterm_st = LCDTERM_ST_WAIT;

	return 0;
}

/*
 * Returns information about framebuffer.
 *
 * Return values:
 *       0 - success
 */
int
lcdterm_fb_info(char **fb_addr, int *fb_size)
{
	/*
	 * 1st byte in framebuffer is reserved - contains
	 * I2C address of framebuffer register.
	 */
	*fb_addr = lcdterm_fb;
	*fb_size = LCDTERM_FB_SIZE;

	return 0;
}
