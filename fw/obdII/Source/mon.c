/*********************************************************************
 *
 * Support for system monitoring and diagnostic.
 *
 *********************************************************************/

#include "xc.h"
#include "HardwareProfile/HardwareProfile.h"
#include "GenericTypeDefs.h"
#include "peripheral/uart.h"

#include "SD-SPI.h"
#include "FSIO.h"

#include "usb.h"
#include "usb_device_local.h"
#include "usb_device.h"

#include "bt.h"
#include "kwp2k.h"
#include "mon.h"
#include "utils.h"
#include "rt_light.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

/* external variables */
extern USB_VOLATILE long USBIntNum;
extern USB_VOLATILE long USBIntSofNum;
extern USB_VOLATILE long USBIntStallNum;
extern USB_VOLATILE long USBIntErrNum;
extern USB_VOLATILE long USBIntTrNum;
extern USB_VOLATILE long USBEP0Service;

/* external functions */
/* print information about memory allocation requests */
extern int print_alloc_requests(int (*printstr)());

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
#define MON_ST_MAIN_MENU	1	/* display main menu */
#define MON_ST_WAIT4INPUT	2	/* wait for user's input */
#define MON_ST_BLUETOOTH	3	/* connect to bluetooth module */

/* prompt */
static const char *mon_diag_prompt = {"> "};
/* main menu */
static const char *mon_diag_main_menu = {
    "------------------------\n\r"
    "[1] Bluetooth module\n\r"
    "[2] Read Memory\n\r"
    "[3] Display task table\n\r"
    "[4] Status of USB\n\r"
    "[5] Status of SD card\n\r"
    "[6] Initialize SD card\n\r"
    "[7] List current directory\n\r"
    "[w] Write test file Test-file.txt\n\r"
    "[d] Detach USB\n\r"
    "[D] Disable USB\n\r"
    "[a] Attach USB\n\r"
    "[t] Display time of the day\n\r"
    "[T] Set date to 04/10/2013 17:00:00\n\r"
//    "[m] Display memory allocation requests\n\r"
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
/* service handle */
static rtl_svc_handle_t mon_sh;

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

/*
 * Prints information about directory or file.
 */
static int
mon_print_finfo(SearchRec *rec)
{
	char s[9];
	int i;

	s[1] = s[2] = s[8] = '\0';
	mon_print_str("\n\rFilename: ");
	for (i = 0; rec->utf16LFNfound[i] != 0 &&
	    i < rec->utf16LFNfoundLength ; i++) {
		s[0] = (char)(rec->utf16LFNfound[i] & 0x7f);
		mon_print_str(s);
	}

	mon_print_str("\n\rShort filename: ");
	mon_print_str(rec->filename);
	mon_print_str("\n\rAttributes: 0x");
	uint2hex(rec->attributes, s, 1);
	mon_print_str(s);
	mon_print_str("\n\rSize: 0x");
	uint2hex(rec->filesize, s, 4);
	mon_print_str(s);
	mon_print_str("\n\rTimestamp: 0x");
	uint2hex(rec->timestamp, s, 4);
	mon_print_str(s);

	return 0;
}

/*
 * Leave diagnostic mode.
 */
static int
mon_diag_leave(void)
{
	mon_diag_mode_enabled = FALSE;

	/* Disable diagnostic service. */
	rtl_svc_disable(mon_sh);

	/* Switch state machine to idle mode. */
	mon_st = MON_ST_IDLE;

	/* Set communication to previous value. */
	kwp2k_set_comm_speed(mon_old_speed);

	return 0;
}

/*
 * Service routine
 */
static int
mon_svc(void)
{
	unsigned char ch;
	unsigned int v;
	char s[9];
	time_t t;
	char *tstr;
	struct tm newtm;
	struct timeval newtv;

	SearchRec rec;
	unsigned char attributes = ATTR_MASK;
	FSFILE *fh;

	switch (mon_st) {
	case MON_ST_IDLE:
		break;

	/* Display main menu */
	case MON_ST_MAIN_MENU:
		mon_print_str(mon_diag_main_menu);
		mon_print_str(mon_diag_prompt);

		mon_st = MON_ST_WAIT4INPUT;
		break;

	/* Wait for user's input */
	case MON_ST_WAIT4INPUT:
		if (mon_getchar(&ch, FALSE) == EAGAIN)
			break;

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

		/* Display service table */
		case '3':
			rtl_svc_display_table(mon_print_str);
			break;

		/* print USB status */
		case '4':
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

		/* print SD card status */
		case '5':
			SD_CS_TRIS = 0;
			if (MDD_SDSPI_MediaDetect())
				mon_print_str("\n\rSD card present.\n\r");
			else
				mon_print_str("\n\rSD card not present.\n\r");

			break;

		/* Initialize SD card and filesystem API */
		case '6':
			if (FSInit())
				mon_print_str("\n\rSD card initialized.\n\r");
			else
				mon_print_str("\n\rSD card initialization"
				    " failed.\n\r");

			break;

		/* Display content of current directory */
		case '7':
			rec.filename[FILE_NAME_SIZE_8P3 + 1] = '\0';

			if (FindFirst("*.*", attributes, &rec) != 0) {
				mon_print_str("\n\rDirectory is empty.\n\r");
				break;
			} else {
				mon_print_finfo(&rec);
			}

			/* Keep listing files */
			while(FindNext(&rec) == 0) {
				mon_print_finfo(&rec);
			}

			mon_print_str("\n\r\n\r");

			break;

		/* Write chunk of data to test file */
		case 'w':
			/* Open file for append. */
			mon_print_str("\n\rOpening file Test-file1.txt... ");
			fh = FSfopen("Test-file1.txt", "a");
			if (fh == NULL) {
				int err = FSerror();

				uint2hex(err, s, 2);
				s[4] = '\0';

				mon_print_str("Failed. err = 0x");
				mon_print_str(s);
				mon_print_str("\n\r");
					
				break;
			} else {
				mon_print_str("Done.");
			}

			/* Write chunk of data to the file. */
			mon_print_str("\n\rWriting to file... ");
			if (FSfwrite("Hello, world\n\r", 1, 14, fh) != 14) {
				int err = FSerror();

				uint2hex(err, s, 2);
				s[4] = '\0';

				mon_print_str("Failed. err = 0x");
				mon_print_str(s);
				mon_print_str("\n\r");
			} else {
				mon_print_str("Done.");
			}

			/* And finally close the file. */
			mon_print_str("\n\rClosing the file... ");
			if (FSfclose(fh) == 0) {
				mon_print_str("Done.\n\r");
			} else {
				int err = FSerror();

				uint2hex(err, s, 2);
				s[4] = '\0';

				mon_print_str("Failed. err = 0x");
				mon_print_str(s);
				mon_print_str("\n\r");
			}

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
/*
		case 't':
			U1CNFG1 = 0x80;
			U1PWRC = 0x01;
			U1CON = 0x01;
			break;
*/
		/* display time of the day */
		case 't':
			t = time(NULL);
			tstr = ctime(&t);
			mon_print_str("Date: ");

			if (tstr == NULL)
				mon_print_str("<NULL>");
			else
				mon_print_str(tstr);

			mon_print_str("\n\r");
			break;

		/* Set date to 04/10/2013 17:00:00 */
		case 'T':
			newtm.tm_sec = 0;
			newtm.tm_min = 0;
			newtm.tm_hour = 17;
			newtm.tm_mday = 10;
			newtm.tm_mon = 3;
			newtm.tm_year = 113;
			newtm.tm_isdst = 0;

			t = mktime(&newtm);

			if (t == (time_t) -1) {
				mon_print_str("Failed to set the date.\n\r");
			} else {
				newtv.tv_sec = t;
				newtv.tv_usec = 0;

				if (settimeofday(&newtv, NULL) == 0)
					mon_print_str("Date set.\n\r");
				else
					mon_print_str("Failed to store "
					    "the date.\n\r");
			}

			break;

#if 0
		/* Display history of memory allocation requests */
		case 'm':
			print_alloc_requests(mon_print_str);
			break;
#endif
		} /* switch */

		mon_st = MON_ST_MAIN_MENU;

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

/* public functions */

/*
 * Initialize the module.
 */
int
mon_init(void)
{
	mon_st = MON_ST_IDLE;

	/* Register, but not enable the service. */
	mon_sh = rtl_svc_init(mon_svc, FALSE, TRUE);

	return 0;
}

/*
 * Enter diagnostic mode.
 */
int
mon_diag_enter(void)
{
	mon_diag_mode_enabled = TRUE;

	/* Enable diagnostic service. */
	rtl_svc_enable(mon_sh);

	/* Wake up state machine. */
	mon_st = MON_ST_MAIN_MENU;

	/* Set communication speed. Save the original one. */
	mon_old_speed = kwp2k_set_comm_speed(115200);

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

