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
#include "mon.h"
#include "utils.h"
#include "rt_light.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

/* type declarations */
/* Groups menu and related handler together. */
typedef struct mon_map_st2menu {
	const char	*menu;			/* menu */
	int		(*h)(unsigned char);	/* handler */
} mon_map_st2menu_t;

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

/* local functions */
/* Handler for main menu. */
static int mon_main_menu(unsigned char);
/* Handler for SD card menu. */
static int mon_sd_menu(unsigned char);

/* state machine states */
enum mon_st_menu_t {
MON_ST_IDLE = 0,	/* machine is IDLE */
MON_ST_MAIN,		/* main menu */
MON_ST_SD_CARD,		/* SD card menu */
MON_ST_BLUETOOTH	/* Bluetooth menu */
} mon_st_menu;

/* prompt */
#define MON_DIAG_PROMPT "> "
/* main menu */
#define MON_DIAG_MAIN_MENU \
    "------------------------\n\r" \
    "[1] SD card\n\r" \
    "[2] Bluetooth module\n\r" \
    "[3] Read Memory\n\r" \
    "[4] Display task table\n\r" \
    "[5] Status of USB\n\r" \
    "[d] Detach USB\n\r" \
    "[D] Disable USB\n\r" \
    "[a] Attach USB\n\r" \
    "[t] Display time of the day\n\r" \
    "[T] Set date to 04/10/2013 17:00:00\n\r" \
    "[0] Quit diagnostic mode\n\r" \
    "------------------------\n\r"

/* SD card submenu */
#define MON_DIAG_SD_MENU \
    "------------------------\n\r" \
    "[1] Status of SD card\n\r" \
    "[2] Initialize SD card\n\r" \
    "[3] List current directory\n\r" \
    "[4] Write test file Test-file.txt\n\r" \
    "[5] Read RFID log file rfid.log\n\r" \
    "[6] Print IO statistics\n\r" \
    "[7] Clear IO statistics\n\r" \
    "[0] Back\n\r" \
    "------------------------\n\r" \

/* Association between state, menu and handler */
static mon_map_st2menu_t mon_st2menu_map[] = {
    {"", NULL},
    {MON_DIAG_MAIN_MENU, mon_main_menu},
    {MON_DIAG_SD_MENU, mon_sd_menu},
    {"", NULL}};

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
 * Sets communication speed (baudrate). Returns previous value.
 */
static int
mon_set_comm_speed(int speed)
{
	int old_speed = U1BRG;

	U1BRG = (SYS_FREQ / 16 / speed - 1);

	return old_speed;
}

/*
 * Handler for main menu.
 *
 */
static int
mon_main_menu(unsigned char ch)
{
	int next_st = MON_ST_MAIN;

	time_t t;
	char *tstr;
	struct tm newtm;
	struct timeval newtv;

	char str[81];

	/* Process input */
	switch (ch) {
	/* Leave diagnostic mode */
	case '0':
		mon_print_str("Leaving diagnostic mode...");
		next_st = -1;
		break;

	/* SD card */
	case '1':
		next_st = MON_ST_SD_CARD;
		break;

	/* Bluetooth module */
	case '2':
		next_st = MON_ST_BLUETOOTH;
		break;

	/* Read memory - for now just print content of config regs */
	case '3':
		snprintf(str, sizeof (str), "DEVCFG3:   0x%08X\n\r", DEVCFG3);
		mon_print_str(str);

		snprintf(str, sizeof (str), "DEVCFG2:   0x%08X\n\r", DEVCFG2);
		mon_print_str(str);

		snprintf(str, sizeof (str), "DEVCFG1:   0x%08X\n\r", DEVCFG1);
		mon_print_str(str);

		snprintf(str, sizeof (str), "DEVCFG0:   0x%08X\n\r", DEVCFG0);
		mon_print_str(str);

		snprintf(str, sizeof (str), "DEVID:     0x%08X\n\r", DEVID);
		mon_print_str(str);

		snprintf(str, sizeof (str), "OSCCON:    0x%08X\n\r", OSCCON);
		mon_print_str(str);

		snprintf(str, sizeof (str), "U1OTGSTAT: 0x%08X\n\r",
		    U1OTGSTAT);
		mon_print_str(str);

		snprintf(str, sizeof (str), "U1OTGCON:  0x%08X\n\r", U1OTGCON);
		mon_print_str(str);

		snprintf(str, sizeof (str), "U1PWRC:    0x%08X\n\r", U1PWRC);
		mon_print_str(str);

		snprintf(str, sizeof (str), "U1CNFG1:   0x%08X\n\r", U1CNFG1);
		mon_print_str(str);

		snprintf(str, sizeof (str), "U1CON:     0x%08X\n\r", U1CON);
		mon_print_str(str);

		break;

	/* Display service table */
	case '4':
		rtl_svc_display_table(mon_print_str);
		break;

	/* print USB status */
	case '5':
		snprintf(str, sizeof (str), "USBDeviceState: %d\n\r",
		    USBDeviceState);
		mon_print_str(str);

		snprintf(str, sizeof (str), "USBIntNum:      %ld\n\r",
		    USBIntNum);
		mon_print_str(str);

		snprintf(str, sizeof (str), "USBIntSofNum:   %ld\n\r",
		    USBIntSofNum);
		mon_print_str(str);

		snprintf(str, sizeof (str), "USBIntStallNum: %ld\n\r",
		    USBIntStallNum);
		mon_print_str(str);

		snprintf(str, sizeof (str), "USBIntErrNum:   %ld\n\r",
		    USBIntErrNum);
		mon_print_str(str);

		snprintf(str, sizeof (str), "USBIntTrNum:    %ld\n\r",
		    USBIntTrNum);
		mon_print_str(str);

		snprintf(str, sizeof (str), "USBEP0Service:  %ld\n\r",
		    USBEP0Service);
		mon_print_str(str);

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

	/* Unknown input */
	default:
		next_st = -2;
	} /* switch */

	return next_st;
}

/*
 * Handler for SD card submenu.
 *
 * Returns FALSE, if we should go back to the main menu,
 * otherwise returns TRUE.
 */
static int
mon_sd_menu(unsigned char ch)
{
	int next_st = MON_ST_SD_CARD;

	char s[9];
	char str[81];

	SearchRec rec;
	unsigned char attributes = ATTR_MASK;
	FSFILE *fh;
	int err;

	switch (ch) {
	/* Go back */
	case '0':
		next_st = MON_ST_MAIN;
		break;

	/* print SD card status */
	case '1':
		SD_CS_TRIS = 0;
		if (MDD_SDSPI_MediaDetect())
			mon_print_str("\n\rSD card present.\n\r");
		else
			mon_print_str("\n\rSD card not present.\n\r");

		break;

	/* Initialize SD card and filesystem API */
	case '2':
		if (FSInit())
			mon_print_str("\n\rSD card initialized.\n\r");
		else
			mon_print_str("\n\rSD card initialization"
			    " failed.\n\r");

		break;

	/* Display content of current directory */
	case '3':
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
	case '4':
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

	/* Read RFID log file */
	case '5':
		/* Open file for reading. */
		mon_print_str("\n\rOpening file rfid.log... ");
		fh = FSfopen("rfid.log", "r");
		if (fh == NULL) {
			err = FSerror();

			snprintf(str, sizeof (str),
			    "Failed. err = %d\n\r", err);

			mon_print_str(str);
					
			break;
		} else {
			mon_print_str("Done.\n\r");
		}

		/* Read the file. */
		mon_print_str("Reading the file...\n\r");

		do {
			int ln;

			ln = FSfread(str, 1, sizeof (str) - 1, fh);
			str[ln] = '\0';

			err = FSerror();

			if ((err != CE_GOOD) && (err != CE_EOF)) {
				snprintf(str, sizeof (str),
				    "\n\rFailed. err = %d\n\r", err);

				mon_print_str(str);

				break;
			}

			mon_print_str(str);
		} while (err != CE_EOF);

		if (err == CE_EOF)
			mon_print_str("\n\rDone.");

		/* And finally close the file. */
		mon_print_str("\n\rClosing the file... ");
		if (FSfclose(fh) == 0) {
			mon_print_str("Done.\n\r");
		} else {
			err = FSerror();

			snprintf(str, sizeof (str),
			    "Failed. err = %d\n\r", err);

			mon_print_str(str);
		}

		break;

	/* Display IO statistics */
	case '6':
		FSkstat_display(mon_print_str);
		break;

	/* Clear IO statistics */
	case '7':
		FSkstat_clear();
		break;

	/* Unknown input */
	default:
		next_st = -2;
	} /* switch */

	return next_st;
}

/*
 * Service routine
 */
static int
mon_svc(void)
{
	unsigned char ch;
	int next;

	/* Wait for user's input - handle Bluetooth menu separately for now. */
	if (mon_st != MON_ST_BLUETOOTH) {
		if (mon_getchar(&ch, FALSE) == EAGAIN)
			return 0;

		mon_print_str("\n\r");
	}

	switch (mon_st) {
	/* Machine idle. */
	case MON_ST_IDLE:
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

	/* Main menu and its submenus */
	default:
		/* Handle current menu */
		next = mon_st2menu_map[mon_st].h(ch);

		switch (next) {
		/* Leave diagnostic menu. */
		case -1:
			mon_diag_leave();
			break;

		/* Unknown input. Display menu again. */
		case -2:
			mon_print_str(mon_st2menu_map[mon_st].menu);
			mon_print_str(MON_DIAG_PROMPT);
			break;

		/* Handle menu transitions */
		default:
			/*
			 * If menu changed, display new menu and prompt.
			 * Otherwise, display just prompt.
			 */
			if (mon_st != next) {
				mon_st = next;

				mon_print_str(mon_st2menu_map[mon_st].menu);
			}

			mon_print_str(MON_DIAG_PROMPT);

			break;
		} /* switch */

		break;
	} /* switch */

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
	mon_sh = rtl_svc_init("Monitor", mon_svc, FALSE, TRUE);

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
	mon_st = MON_ST_MAIN;

	/* Set communication speed. Save the original one. */
	mon_old_speed = mon_set_comm_speed(115200);

	/* Display main menu. */
	mon_print_str(MON_DIAG_MAIN_MENU);
	mon_print_str(MON_DIAG_PROMPT);

	return 0;
}

/*
 * Leave diagnostic mode.
 */
int
mon_diag_leave(void)
{
	mon_diag_mode_enabled = FALSE;

	/* Disable diagnostic service. */
	rtl_svc_disable(mon_sh);

	/* Switch state machine to idle mode. */
	mon_st = MON_ST_IDLE;

	/* Set communication to previous value. */
	mon_set_comm_speed(mon_old_speed);

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

