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

/* macros */

/* local types */

/* local variables */
/* state machine handling communication with the terminal */
static enum {
	LCDTERM_ST_IDLE = 0,	/* state machine idle */

	LCDTERM_ST_SENDFB1,	/* Write framebuffer */
	LCDTERM_ST_SENDFB2
} lcdterm_st;

/*
 * frame buffer - null terminated 16 character string,
 * 1st character reserved for FB I2C address
 */
static char lcdterm_fb[16 + 1 + 1];

/* local functions */

/*
 * Main service routine - called periodically from timer interrupt.
 * Handles communication with LCDTERM.
 */
static int
lcdterm_svc(void)
{
	switch (lcdterm_st) {
	/* service idle */
	case LCDTERM_ST_IDLE:
		break;

	/*
	 * Send framebuffer to the terminal - phase 1.
	 *
	 * Grab I2C bus.
	 */
	case LCDTERM_ST_SENDFB1:
		if (i2c_request_bus() != 0)
			break;

		/* Prepare data */
		lcdterm_fb[0] = LCDTERM_REG_FB;

		lcdterm_st = LCDTERM_ST_SENDFB2;
		break;

	/*
	 * Send framebuffer to the terminal - phase 2.
	 *
	 * Send framebuffer to the terminal. First byte in data buffer
	 * contains address of framebuffer register. Do not send the last
	 * zero byte terminating the string.
	 * The terminal automatically increments address pointer after
	 * each byte is processed.
	 */
	case LCDTERM_ST_SENDFB2:
		if (i2c_write(LCDTERM_I2C_ADDRESS,
		    (unsigned char *) lcdterm_fb,
		    sizeof (lcdterm_fb) - 1) == EAGAIN)
			break;

		/* Release I2C bus */
		i2c_release_bus();

		lcdterm_st = LCDTERM_ST_IDLE;
		break;

	} /* switch */

	return 0;
}

/*
 * Returns TRUE if the service is currently busy. Otherwise returns FALSE.
 */
static BOOL
lcdterm_svc_busy(void)
{
	return (lcdterm_st != LCDTERM_ST_IDLE);
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

	/* State machine is IDLE */
	lcdterm_st = LCDTERM_ST_IDLE;

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
	*fb_addr = lcdterm_fb + 1;
	*fb_size = sizeof (lcdterm_fb) - 1;

	return 0;
}

/*
 * Transfers framebuffer to the terminal.
 *
 * Return values:
 *      0 - operation successfully commenced
 *  EBUSY - module currently busy
 */
int
lcdterm_fb_write(void)
{
	/* Module busy. */
	if (lcdterm_svc_busy()) {
		return EBUSY;
	}

	/* Kick off the process. */
	lcdterm_st = LCDTERM_ST_SENDFB1;

	return 0;
}
