#include "HardwareProfile/HardwareProfile.h"
#include "GenericTypeDefs.h"
#include "peripheral/uart.h"
#include "bt.h"
#include "xc.h"
#include "mon.h"
#include <stdlib.h>
#include <stdio.h>

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
#define MON_ST_IDLE	0	/* machine is IDLE */
#define MON_ST_RX	1	/* receive character */
#define MON_ST_TX	2	/* transmit character */
#define MON_ST_TX_ECHO	3	/* wait for TX echo and discard it */

/* local variables */
/* SW transmit buffer - just one character for now */
static unsigned char mon_tx_buffer[1];
/* number of echo characters we are waiting for */
static unsigned int mon_tx_echo_num;
/* state of main state machine */
static int mon_st;

/*
 *
 */
int
mon_init(void)
{
	/* initialize UART1 used for communication with monitor
	   - baudrate: 115200
	   - 8 data bits, 1 stop bit
	 */
	U1BRG = (SYS_FREQ / 16 / 115200 - 1);
//	U1MODE = UART_EN | UART_EN_WAKE;
	U1MODE = UART_EN;

	/* TX pin is inverted, so configure it as active in LOW */
	U1STA = (UART_RX_ENABLE | UART_TX_ENABLE | UART_TX_INV);

	/* State machine is idle */
	mon_st = MON_ST_RX;

	/* nothing had been sent yet, no echos expected */
	mon_tx_echo_num = 0;

	return MON_OK;
}

/*
 * Check if there is a character in receive buffer.
 * Return TRUE, if character is received, otherwise return FALSE.
 * As a side effect, clears receive errors.
 */
BOOL
mon_rx_rdy(void)
{
	unsigned char dummy;

	/* handle receive errors */
	if (U1STA & 0x000E) {
		/* dummy read to clear FERR/PERR */
		dummy = U1RXREG;
		/* clear OERR to keep receiving */
		U1STAbits.OERR = 0;
	}

	return (U1STAbits.URXDA);
}

/*
 * Send character to upper module.
 * Do not block if there is no space in transmit buffer, return MON_TX_FULL
 * in that case. If character can be put into transmit buffer return MON_OK.
 */
int
mon_putchar(unsigned char ch)
{
	/* busy with sending previous character, return */
	if (mon_st == MON_ST_TX)
		return MON_TX_FULL;

	/* If there is no space in HW TX buffer, postpone transmission */
	if (U1STAbits.UTXBF) {
		mon_tx_buffer[0] = ch;
		mon_st = MON_ST_TX;

		return MON_OK;
	}

	/* There is a space in HW TX buffer */
	U1TXREG = ch;

	/*
	 * Since we are running in half duplex mode, every sent character
	 * is echoed back. Wait for that echo and discard it.
	 */
	mon_tx_echo_num++;
	mon_st = MON_ST_TX_ECHO;

	return MON_OK;
}

/*
 *
 */
int
mon_getchar(unsigned char *ch, int debug)
{
	unsigned char dummy;

	/* handle receive errors */
	if (U1STA & 0x000E) {
		/* dummy read to clear FERR/PERR */
		dummy = U1RXREG;
		/* clear OERR to keep receiving */
		U1STAbits.OERR = 0;
	}

	if (!U1STAbits.URXDA)
		return MON_RX_EMPTY;

	/* get data from UART RX FIFO */
	*ch = U1RXREG;

/*
	if (debug) {
		hex2ascii(*ch);
	}
*/
	return MON_OK;
}

/*
 * Service routine
 */
int
mon_service(void)
{
	unsigned char ch, dummy;

	switch (mon_st) {
	case MON_ST_IDLE:
		break;

	/* Receiver */
	case MON_ST_RX:
	/* Characters received, process them */
	while (mon_getchar(&ch, 0) == MON_OK) {
		if (ch == 'S') {
			/* assert RESET signal */
			LATBSET = 0x8000;
		} else if (ch == 's') {
			/* deactivate RESET signal */
			LATBCLR = 0x8000;
		} else if (ch == 'q') {
			unsigned int d;

			/* jump to bootloader by triggering SOFT RESET */

			/* disable interrupts first */
			asm volatile("di");

			/* Suspend BT module */
			bt_suspend();

			 /* write invalid key to force lock */
			SYSKEY = 0x00000000;
			/* Key 1 */
			SYSKEY = 0xAA996655;
			/* Key 2 */
			SYSKEY = 0x556699AA;

			/* set SWRST bit to arm reset */
			RSWRSTSET = 1;
			/* read RSWRST register to trigger reset */
			d = RSWRST;
			/*
			 * prevent any unwanted code execution
			 * until reset occurs
			 */
			while(1);
		} else {
			/*
			 * Do not check for a return value, assume there is
			 * always a space in TX buffer
			 */
			if (bt_putchar(ch) != BT_OK)
				LATBCLR = 1 << 12;
		}
	}
	break;

	/* Transmitter */
	/*
	 * Wait for a space in HW TX buffer, then transmit character
	 * waiting in SW buffer
	 */
	case MON_ST_TX:
		/* There is no space in transmit buffer, try later */
		if (U1STAbits.UTXBF)
			break;

		U1TXREG = mon_tx_buffer[0];

		/* handle echo */
		mon_tx_echo_num++;
		mon_st = MON_ST_TX_ECHO;
		break;

	/*
	 * Since we are running in half duplex mode, every transmitted
	 * character is echoed back. Wait for those echoes and discard them.
	 */
	case MON_ST_TX_ECHO:
		if ((mon_getchar(&dummy, 0) == MON_OK)
		    && (--mon_tx_echo_num == 0))
			mon_st = MON_ST_RX;
		break;

	/* Shouldn't happen, but... */
	default:
		break;
	}

	return MON_OK;
}

