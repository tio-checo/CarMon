#include "HardwareProfile/HardwareProfile.h"
#include "GenericTypeDefs.h"
#include "peripheral/uart.h"
#include "kwp2k.h"
#include "rt_light.h"
#include "xc.h"
#include <stdlib.h>
#include <stdio.h>

/* kwp2k_st states */
#define KWP2K_ST_IDLE		0	/* machine IDLE */
#define KWP2K_ST_SEND_WUP_LOW	1	/* sending Wake up pattern - low */
#define KWP2K_ST_SEND_WUP_HIGH	2	/* sending Wake up pattern - high */
#define KWP2K_ST_SEND_DATA1	3	/* sending data to ECU - phase 1 */
#define KWP2K_ST_SEND_DATA2	4	/* sending data to ECU - phase 2 */

/* communication speed */
#define KWP2K_BAUDRATE		10400

/* communication pins */
#define KWP2K_RX_PIN		2	/* RD2 */
#define KWP2K_RX_MASK		(1 << KWP2K_RX_PIN)
#define KWP2K_TX_PIN		3	/* RD3 */
#define KWP2K_TX_MASK		(1 << KWP2K_TX_PIN)

/* local variables */
/* pointer to character in transmit buffer currently being sent to ECU */
static unsigned char *kwp2k_tx_head;
/* total number of characters to be sent to ECU */
static int kwp2k_tx_ln;

/*
 * Parameters defining various timing aspects of KWP2000 packet
 * with 1us resolution. Variables are initialized in kwp2k_init().
 */
/* Inter byte time in ECU request. Not used for now */
/* static unsigned long kwp2k_p1; */
/*
 * Time between end of ECU response and and start of new tester request,
 * or time between end of tester request and start of new tester request
 * if ECU fails to respond. Shall be measured from the last byte in the
 * latest response message from any ECU responding.
 */
/* static unsigned long kwp2k_p3; Not used for now */
/* P4 - inter byte time in tester request. */
static unsigned long kwp2k_p4;

/* Timers */
static rtl_timer_t *kwp2k_l2_tx_ptimer;

/* main state machine */
static int kwp2k_st;

/* local functions */

/*
 * Transition state machine to another state.
 */
static int
kwp2k_goto(int new_state)
{
	kwp2k_st = new_state;

	return KWP2K_OK;
}

/* public functions */

/*
 * Initialize KWP2000 communication channel
 *  - UART1 is used to communicate with ECU
 *  - set baudrate to 10200Bd
 */
int
kwp2k_init(void)
{
	/* state machine is IDLE */
	kwp2k_goto(KWP2K_ST_IDLE);

	/*
	 * initialize IO pins
	 */
	/* set Tx to inactive before it is configured as digital output */
	LATDCLR = KWP2K_TX_MASK;
	TRISDCLR = KWP2K_TX_MASK;

	/*
         * initialize UART1 used for communication with ECU
	 *  - baudrate: KWP2K_BAUDRATE
	 *  - 8 data bits, 1 stop bit
	 *  - continue to operate in IDLE mode
	 *  - IrDA mode disabled
	 *  - HW flow control turned off
	 *  - loopback mode disabled
	 *  - autobaud mode disabled
	 *  - standard speed mode - 16x baud clock enabled
	 */
	U1BRG = (SYS_FREQ / 16 / KWP2K_BAUDRATE - 1);
	U1MODE = UART_EN;

	/* TX pin is inverted, so configure it as active in LOW */
	U1STA = (UART_RX_ENABLE | UART_TX_ENABLE | UART_TX_INV);

	/* Initialize timing parameters */
	/* P4 - inter byte time in tester request */
	kwp2k_p4 = KWP2K_T_P4_DEFMIN;

	kwp2k_l2_tx_ptimer = NULL;
	rtl_timer_init(&kwp2k_l2_tx_ptimer, 0);

	return KWP2K_OK;
}

/*
 * Suspend the module.
 */
int
kwp2k_suspend(void)
{
	/* Before turning off UART, make sure Tx line is inactive */
	LATDCLR = KWP2K_TX_MASK;

	/* turn off KWP2000 UART */
	U1STA = UART_DIS;
	U1MODE = UART_DIS;

	return KWP2K_OK;
}

/*
 * Directly controls K line - used when sending Wake up Pattern.
 */
int
kwp2k_set_kline(BOOL active)
{
	if (active) {
		/* prepare desired value to be sent on Kline */
		LATDSET = KWP2K_TX_MASK;

		/*
		 * Now disable UART transmitter, so that TX ping can be
		 * controlled as regular digital output.
		 */
		U1STACLR = UART_TX_ENABLE;
	} else {
		/*
		 * Deactivate Kline, then re-enable UART.
		 */
		LATDCLR = KWP2K_TX_MASK;
		U1STASET = UART_TX_ENABLE;
	}

	return KWP2K_OK;
}

/*
 * Return character received from ECU. Signal also receive errors.
 */
int
kwp2k_getchar(kwp2k_rx_char_t *k2p2k_ch)
{
	/* Receive buffer is empty */
	if (!U1STAbits.URXDA) {
		/* If receiving was disabled due to 'buffer overflow',
		 * re-enable it by clearing OERR error bit.
		 */
		if (U1STAbits.OERR)
			U1STAbits.OERR = 0;

		return KWP2K_RX_EMPTY;
	}

	/*
	 * There is a character in receive buffer, return it along
	 * with related status error bits. Need to get error bits first -
	 * they are lost once character is pulled from buffer.
	 */
	k2p2k_ch->sts = U1STA & 0x000E;
	k2p2k_ch->ch = U1RXREG;

	return KWP2K_OK;
}

/*
 * Send KWP2000 packet to ECU. Optionally prepend it with Wake up pattern.
 */
int
kwp2k_send_packet(unsigned char *tx_buff, int ln, BOOL send_wup)
{
	/* still sending previous packet */
	if (kwp2k_st != KWP2K_ST_IDLE)
		return KWP2K_TX_BUSY;

	/* If there is nothing to be sent, return */
	if (ln == 0)
		return KWP2K_OK;

	kwp2k_tx_head = tx_buff;
	kwp2k_tx_ln = ln;

	/* disable receiver during transmission, echoes are discarded */
	U1STACLR = UART_RX_ENABLE;

	if (send_wup)
		kwp2k_goto(KWP2K_ST_SEND_WUP_LOW);
	else
		kwp2k_goto(KWP2K_ST_SEND_DATA1);

	return KWP2K_OK;
}

/*
 * Check if transmission of packet to ECU is finished.
 */
BOOL
kwp2k_transmitter_busy(void)
{
	return (kwp2k_st != KWP2K_ST_IDLE);
}

/*
 * Service routine
 * - handles communication with ECU
 */
int
kwp2k_service(void)
{
	switch (kwp2k_st) {
	case KWP2K_ST_IDLE:
		break;

	/* send Wake up Pattern - low pulse */
	case KWP2K_ST_SEND_WUP_LOW:
		/* activate Kline - that also disables UART */
		kwp2k_set_kline(TRUE);

		/* activate timer - will measure 1st half length of WuP */
		rtl_timer_init(&kwp2k_l2_tx_ptimer, KWP2K_T_WUP / 2);

		kwp2k_goto(KWP2K_ST_SEND_WUP_HIGH);
		break;

	/* send Wake up Pattern - high pulse */
	case KWP2K_ST_SEND_WUP_HIGH:
		/* wait until sending of low pulse is done */
		if (!rtl_timer_expired(kwp2k_l2_tx_ptimer))
			break;

		/* deactivate Kline - that also enables UART */
		kwp2k_set_kline(FALSE);

		/* activate timer - will measure 2nd half of WuP */
		rtl_timer_init(&kwp2k_l2_tx_ptimer, KWP2K_T_WUP / 2);

		kwp2k_goto(KWP2K_ST_SEND_DATA1);
		break;

	/* sending data to ECU */
	case KWP2K_ST_SEND_DATA1:
		/* wait until line is idle for desired amount of time */
		if (!rtl_timer_expired(kwp2k_l2_tx_ptimer))
			break;

		/* Send next character from buffer */
		U1TXREG = *kwp2k_tx_head;

		/* wait until byte it is transmitted */		
		kwp2k_goto(KWP2K_ST_SEND_DATA2);

		break;

	/* wait until transmission of byte is finished */
	case KWP2K_ST_SEND_DATA2:
		if (!U1STAbits.TRMT)
			break;

		/*
		 * Line has to be silent for a while before next byte
		 * can be sent out.
		 */
		rtl_timer_init(&kwp2k_l2_tx_ptimer, kwp2k_p4);

		/* Move to the next byte */
		kwp2k_tx_ln--;
		kwp2k_tx_head++;

		/* If there is nothing to be sent, switch to IDLE mode */
		if (kwp2k_tx_ln == 0) {
			/*
			 * Transmission is finished, so re-enable receiver.
			 */
			U1STASET = UART_RX_ENABLE;

			kwp2k_goto(KWP2K_ST_IDLE);
		} else {
			kwp2k_goto(KWP2K_ST_SEND_DATA1);
		}

		break;
	}

	return KWP2K_OK;
}

