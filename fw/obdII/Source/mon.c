/*********************************************************************
 *
 * Support for system monitoring and diagnostic.
 *
 *********************************************************************/

#include "xc.h"
#include "HardwareProfile/HardwareProfile.h"
#include "GenericTypeDefs.h"
#include "peripheral/uart.h"

#include "usb.h"
#include "usb_device_local.h"
#include "usb_device.h"

#include "bt.h"
#include "kwp2k.h"
#include "mon.h"
#include "utils.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

extern USB_VOLATILE long USBIntNum;
extern USB_VOLATILE long USBIntSofNum;
extern USB_VOLATILE long USBIntStallNum;
extern USB_VOLATILE long USBIntErrNum;
extern USB_VOLATILE long USBIntTrNum;
extern USB_VOLATILE long USBEP0Service;

/*
static const char hex2ascii_tbl[16] = {'0', '1', '2', '3', '4', '5', '6',
    '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

static int hex2ascii(unsigned char b)
{
	mon_putchar('^');
	mon_putchar(hex2ascii_tbl[(b >> 4) & 0x0f]);
	mon_putchar(hex2ascii_tbl[b & 0x0f]);
	mon_putchar('$');
}
*/

/* state machine states */
#define MON_ST_IDLE		0	/* machine is IDLE */
#define MON_ST_MAIN_MENU	1	/* main menu */
#define MON_ST_BLUETOOTH	2	/* connect to bluetooth module */

/* prompt */
static const char *mon_diag_prompt = {"> "};
/* main menu */
static const char *mon_diag_main_menu = {
    "------------------------\n\r"
    "[1] Bluetooth module\n\r"
    "[2] Read Memory\n\r"
    "[3] Status of USB\n\r"
    "[d] Detach USB\n\r"
    "[D] Disable USB\n\r"
    "[a] Attach USB\n\r"
    "[t] Test USB\n\r"
    "[0] Quit diagnostic mode\n\r"
    "------------------------\n\r"
};
/* bluetooth menu - NOT USED for now */
/*
static const char *mon_diag_bluetooth_menu = {
    "[0] Quit\n"
    "------------------------\n"
};
*/

/* Read memory menu  - NOT USED for now*/
/*
static const char *mon_diag_memory_menu = {
    "[0] Quit\n"
    "------------------------\n"
};
*/

/* local variables */
/* TRUE if running in diagnostic mode */
static BOOL mon_diag_mode_enabled;
/* original communication speed of diagnostic communication port */
static int mon_old_speed;
/* state of main state machine */
static int mon_st;

/* private functions */

/*
 * Wait for character. If block is TRUE, blocks if the receive buffer is empty.
 */
static int
mon_getchar(unsigned char *ch, BOOL block)
{
	unsigned char dummy;

	/* handle receive errors */
	if (U1STA & 0x000E) {
		/* dummy read to clear FERR/PERR */
		dummy = U1RXREG;
		/* clear OERR to keep receiving */
		U1STAbits.OERR = 0;
	}

	/* Block only if required */
	if (!block && !U1STAbits.URXDA)
		return EAGAIN;

	/* Block until character is available */
	while (!U1STAbits.URXDA);

	/* get data from UART RX FIFO */
	*ch = U1RXREG;

	return 0;
}

/*
 * Sends character to diagnostic communication port.
 * Wait until transmission of character is done.
 */
static int
mon_putchar(unsigned char ch)
{
	unsigned char echo;

	/* Wait until transmitter is ready */
	while (!U1STAbits.TRMT);

	/* Send out the character */
	U1TXREG = ch;

	/*
	 * Since we are running in half duplex mode, every sent character
	 * is echoed back. Wait for that echo and discard it.
	 */
	mon_getchar(&echo, TRUE);

	return 0;
}

/*
 * Prints string (array of chars terminated with '\0').
 */
static int
mon_print_str(const char *str)
{
	int i;

	for (i = 0; str[i] != '\0'; i++)
		mon_putchar(str[i]);

	return 0;
}

/* public functions */

/*
 * Initialize the module.
 */
int
mon_init(void)
{
	mon_st = MON_ST_IDLE;

	return 0;
}

/*
 * Enter diagnostic mode.
 */
int
mon_diag_enter(void)
{
	mon_diag_mode_enabled = TRUE;

	/* Wake up state machine. */
	mon_st = MON_ST_MAIN_MENU;

	/* Set communication speed. Save the original one. */
	mon_old_speed = kwp2k_set_comm_speed(115200);

	return 0;
}

/*
 * Leave diagnostic mode.
 */
int
mon_diag_leave(void)
{
	mon_diag_mode_enabled = FALSE;

	/* Switch state machine to idle mode. */
	mon_st = MON_ST_IDLE;

	/* Set communication to previous value. */
	kwp2k_set_comm_speed(mon_old_speed);

	return 0;
}

/*
 * Return TRUE if running in diagnostic mode. Otherwise return FALSE.
 */
BOOL
mon_is_diag_mode(void)
{
	return mon_diag_mode_enabled;
}

/*
 * Service routine
 */
int
mon_svc(void)
{
	unsigned char ch;
	unsigned int v;
	char s[9];

	switch (mon_st) {
	case MON_ST_IDLE:
		break;

	/* Display main menu, then wait for input */
	case MON_ST_MAIN_MENU:
		mon_print_str(mon_diag_main_menu);
		mon_print_str(mon_diag_prompt);
		mon_getchar(&ch, TRUE);
		mon_print_str("\n\r");

		/* Process input */
		switch (ch) {
		/* Leave diagnostic mode */
		case '0':
			mon_diag_leave();
			break;

		/* Diagnose bluetooth module */
		case '1':
			mon_st = MON_ST_BLUETOOTH;
			break;

		/* Read memory - for now just print content of config regs */
		case '2':
			s[8] = '\0';

			v = DEVCFG3;
			uint2hex(v, s, 4);
			mon_print_str("DEVCFG3: 0x");
			mon_print_str(s);

			v = DEVCFG2;
			uint2hex(v, s, 4);
			mon_print_str("\n\rDEVCFG2: 0x");
			mon_print_str(s);

			v = DEVCFG1;
			uint2hex(v, s, 4);
			mon_print_str("\n\rDEVCFG1: 0x");
			mon_print_str(s);

			v = DEVCFG0;
			uint2hex(v, s, 4);
			mon_print_str("\n\rDEVCFG0: 0x");
			mon_print_str(s);

			v = DEVID;
			uint2hex(v, s, 4);
			mon_print_str("\n\rDEVID: 0x");
			mon_print_str(s);

			v = OSCCON;
			uint2hex(v, s, 4);
			mon_print_str("\n\rOSCCON: 0x");
			mon_print_str(s);

			v = U1OTGSTAT;
			uint2hex(v, s, 4);
			mon_print_str("\n\rU1OTGSTAT: 0x");
			mon_print_str(s);

			v = U1OTGCON;
			uint2hex(v, s, 4);
			mon_print_str("\n\rU1OTGCON: 0x");
			mon_print_str(s);

			v = U1PWRC;
			uint2hex(v, s, 4);
			mon_print_str("\n\rU1PWRC: 0x");
			mon_print_str(s);

			v = U1CNFG1;
			uint2hex(v, s, 4);
			mon_print_str("\n\rU1CNFG1: 0x");
			mon_print_str(s);

			v = U1CON;
			uint2hex(v, s, 4);
			mon_print_str("\n\rU1CON: 0x");
			mon_print_str(s);

			mon_print_str("\n\r");

			break;

		/* print USB status */
		case '3':
			s[8] = '\0';

			v = USBDeviceState;
			uint2hex(v, s, 4);
			mon_print_str("USBDeviceState: 0x");
			mon_print_str(s);

			v = USBIntNum;
			uint2hex(v, s, 4);
			mon_print_str("\n\rUSBIntNum: 0x");
			mon_print_str(s);

			v = USBIntSofNum;
			uint2hex(v, s, 4);
			mon_print_str("\n\rUSBIntSofNum: 0x");
			mon_print_str(s);

			v = USBIntStallNum;
			uint2hex(v, s, 4);
			mon_print_str("\n\rUSBIntStallNum: 0x");
			mon_print_str(s);

			v = USBIntErrNum;
			uint2hex(v, s, 4);
			mon_print_str("\n\rUSBIntErrNum: 0x");
			mon_print_str(s);

			v = USBIntTrNum;
			uint2hex(v, s, 4);
			mon_print_str("\n\rUSBIntTrNum: 0x");
			mon_print_str(s);

			v = USBEP0Service;
			uint2hex(v, s, 4);
			mon_print_str("\n\rUSBEP0Service: 0x");
			mon_print_str(s);

			mon_print_str("\n\r");

			break;

		/* detach USB */
		case 'd':
			USBDeviceDetach();
			break;

		/* attach USB */
		case 'a':
			USBDeviceAttach();
			break;

		/* disable USB */
		case 'D':
			USBModuleDisable();
			break;

		/* generate test eye pattern on USB bus */
		case 't':
			U1CNFG1 = 0x80;
			U1PWRC = 0x01;
			U1CON = 0x01;
		}

		break;

	/*
	 * Diagnose bluetooth module. For now just forward characters between
	 * bluetooth module and diagnostic port.
	 */
	case MON_ST_BLUETOOTH:
		while (1) {
			if (bt_getchar(&ch) == 0)
				mon_putchar(ch);

			if (mon_getchar(&ch, FALSE) == 0)
				bt_putchar(ch);
		}

		break;

	/* Shouldn't happen, but... */
	default:
		break;
	}

	return 0;
}

