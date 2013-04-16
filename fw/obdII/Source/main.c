/*********************************************************************
 *
 * Main program.
 *
 *********************************************************************/

#include "HardwareProfile.h"
#include "xc.h"

#include "bt.h"
#include "data_acq.h"
#include "kwp2k.h"
#include "master_comm.h"
#include "mon.h"
#include "obd.h"
#include "pic_core.h"
#include "rt_light.h"
#include "usb_app.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

/* global variables */
int errno;

/* local functions */
/*
 * initialize peripheries
 */
static int
per_init(void)
{
	/* configure JTAG pins as GPIO - they drive LEDs */
	DDPCONbits.JTAGEN = 0;
	/*
	 * - turn off both LEDs
	 * - configure unconnected pins as outputs (there are no internal
	 *   pull-ups, pull-downs).
	 */
	LATBSET = 0x3000;
	/* RB outputs: 13,12,11,9,6,5,4,3,2,1,0 */
	TRISBCLR = 0x3a7f;

	/*
	 * Initialize interrupt controller
	 *  - enable multi-vector mode
	 *  - set vector spacing to 32
         */
	INTCON = 0x00041000;

	/*
	 * Initialize uSD card pins. Attach weak pull-ups to SPI pins.
	 * Card detect pins are not connected to CNs pins, so they
	 * can't be "pulled up". Will need to add external pull-ups.
	 */
#if 0
	CNPUEbits.CNPUE8 = 1;	/* SCK2 - RG6 */
	CNPUEbits.CNPUE9 = 1;	/* SDI2 - RG7 */
	CNPUEbits.CNPUE10 = 1;	/* SDO2 - RG8 */
	CNPUEbits.CNPUE11 = 1;	/* *SS2 - RG9 */

	CNENbits.CNEN8 = 1;	/* SCK2 - RG6 */
	CNENbits.CNEN9 = 1;	/* SDI2 - RG7 */
	CNENbits.CNEN10 = 1;	/* SDO2 - RG8 */
	CNENbits.CNEN11 = 1;	/* *SS2 - RG9 */

	CNCONbits.ON = 1;

	TRISG = 0x3c0;
#endif

	SD_CD1_TRIS = 1;
	SD_CD2_TRIS = 1;

	/*
	 * CD pins are unusable w/o pull-ups, thus assume that SD Card
	 * is inserted. Power it on and connect inactive chip select signal.
	 */
	SD_PWR = 0;
	SD_PWR_TRIS = 0;

	SD_CS = 1;
//	SD_CS_TRIS = 0;
	SD_CS_TRIS = 1;

	return	0;
}

/*
 * main()
 */
int
main(int argc, char **argv)
{
	BOOL sleeping = FALSE;

	/* Init phase */
	per_init();
        rtl_init();
	bt_init();
	daq_init();
	kwp2k_init();
	mcom_init();
	mon_init();
	obd_init();
	pcore_init();

	/* USB module */
	uapp_init();

	/* Immediately enter diagnostic mode */
/*
	mon_diag_enter();
	while (mon_is_diag_mode()) {
		mon_svc();
	}
*/

	while(1) {
		if (mcom_suspend()) {
			sleeping = TRUE;

			asm volatile("di");

			/* Suspend real time module */
			rtl_suspend();

			/* Suspend KWP module */
			kwp2k_suspend();

			/* Suspend BT module */
			bt_suspend();

			/* Suspend data acquisition module */
			daq_suspend();

			/* turn off LEDs */
			LATBSET = (1 << 12);
			LATBCLR = (1 << 13);
			TRISBSET = 0x3000;
			/* unlock OSCCON register */
			/* write invalid key to force lock */
			SYSKEY = 0x00000000;
			/* Key 1 */
			SYSKEY = 0xAA996655;
			/* Key 2 */
			SYSKEY = 0x556699AA;

			/* Subsequent WAIT will activate sleep mode. */
			OSCCONSET = (1 << 4);	/* go to sleep */

			/* enable watchdog */
			WDTCONSET = (1 << 15);
			WDTCONSET = 1;

		}

		/* Run post processing upper layer service routine. */
		daq_pp_upper_svc();

		/*
		 * Enter idle mode if there is nothing to do.
		 * Alternatively, enter sleep mode if appropriate conditions
		 * are met.
		 */
		_wait();

		/*
		 * If this is Watchdog wake up from sleep, turn on LEDs
		 */
		while (sleeping) {
//			sleeping = FALSE;

			if (RCON & 0x10)
				RCONCLR = 0x10;

			if (RCON & 0x0c)
				RCONCLR = 0x0c;

			asm volatile("di");

			LATBINV = 0x3000;
			TRISBCLR = 0x3000;
			_wait();
//				while (1)
//					WDTCONSET = 1;
//			}
		}
	}

	/* not reached */
	return 0;
}

