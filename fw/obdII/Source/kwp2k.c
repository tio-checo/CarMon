/*********************************************************************
 *
 * Handles communication with ECU using KWP2000 protocol. Implements Data
 * Link Layer (OSI layer 2) as specified in following ISO 14230-2 document:
 *
 *  http://www.alfa145.co.uk/obd/14230-2s.pdf
 *
 * Implementation is divided into two sub-layers
 *  o lower layer (ll)
 *    - transmit Wake up Pattern to ECU (part of "fast initialization")
 *    - transmit of data byte to ECU honoring P4 timing parameter
 *      (see below)
 *    - receives byte from ECU
 *
 *  o upper layer (ul)
 *    - assembles and transmits complete packet to ECU
 *    - provides for "on-demand" initialization of communication with ECU
 *    - receives and verifies packet from ECU
 *
 * Related to that, there are two modes of operation. The simple one which
 * just passes characters between ECU and master without any processing
 * and the more sophisticated one which takes care of following:
 *  - takes care of assembling KWP2000 packet - creates header, calculates
 *    checksum.
 *  - automatically on-demand initializes communication with ECU
 *  - makes sure various timing parameters are honored.
 * 
 * KWP2000 - message structure
 * --------------------------------             -------
 * | Fmt | Tgt | Src | Len | SId | ... data ...  | CS |
 * --------------------------------             -------
 *
 * Format Byte
 * msb                   lsb
 * -------------------------
 * |A1|A0|L5|L4|L3|L2|L1|L0|
 * -------------------------
 *
 * hdr mode A1 A0   description
 *    2      1  0 Header w/ address information, physical target address
 *    3      1  1 Header w/ address information, functional target address
 *
 *
 * L5-L0 length of data field of the message including SId (Service
 * Identification byte excluding Checksum byte
 *
 * Checksum byte
 *  - simple 8-bit sum of all bytes in the message, excluding CS itself.
 *
 *--------------------------------
 * TIMING
 *
 * Tester            ECU1        ECU2       Tester
 * request         response    response     request
 *______________________________________________________
 * U V V ... U    U V ... U    U V ... U    U V ... U
 * \ /        \  /\ /      \  /         \  /
 *  P4         P2  P1       P2           P3
 *
 *********************************************************************/

#include "HardwareProfile.h"
#include "GenericTypeDefs.h"
#include "xc.h"
#include "uart.h"

#include "kwp2k.h"
#include "rt_light.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

/* kwp2k_ll_st states */
#define KWP_STLL_IDLE		0	/* machine IDLE */
#define KWP_STLL_RX_WAIT4PKT	1	/* wait for start of ECU response */
#define KWP_STLL_RX_PKT		2	/* receive packet from ECU */
#define KWP_STLL_TX_WUP_LOW	3	/* send WuP - low pulse */
#define KWP_STLL_TX_WUP_HIGH	4	/* send WuP - high pulse */
#define KWP_STLL_TX_PKT1	5	/* send packet to ECU - 1st byte */
#define KWP_STLL_TX_PKT2	6	/* send packet to ECU - next bytes */

/* kwp2k_ul_st states */
#define KWP_STUL_IDLE		0	/* machine IDLE */
#define KWP_STUL_COMM_INIT	1	/* initialize communication with ECU */
#define KWP_STUL_COMM_INIT_RESP	2	/* wait for response from ECU */
#define KWP_STUL_SEND		3	/* send packet to ECU */
#define KWP_STUL_RECV		4	/* receive packets from ECU */
/* wait until packet is completely transmitted to ECU */
#define KWP_STUL_WAIT4TXDONE	5

/* communication speed */
/* for debug purposes, select speed supported by debug tool */
#define KWP2K_BAUDRATE_NORMAL	10400
#define KWP2K_BAUDRATE_DEBUG	9600

/* communication pins */
#define KWP2K_RX_PIN		2	/* RD2 */
#define KWP2K_RX_MASK		(1 << KWP2K_RX_PIN)
#define KWP2K_TX_PIN		3	/* RD3 */
#define KWP2K_TX_MASK		(1 << KWP2K_TX_PIN)

/* local variables */
/* indicates if communication with ECU is established */
static BOOL kwp2k_comm_established;

/* lower layer communication object */
static kwp2k_rx_tx_t kwp2k_ll_comm;
/* upper layer communication object */
static kwp2k_rx_tx_t kwp2k_ul_comm;

/* communication mode - BASIC vs ENHANCED */
static int kwp2k_comm_mode;

/*
 * Structure encapsulating various parameters related to communication
 * with ECU.
 */
static struct kwp2k_params_t {
	/* ECU address */
	unsigned char ecu_address;
	/* tester address */
	unsigned char tester_address;
} kwp2k_params;

/*
 * Parameters defining various timing aspects of KWP2000 packet
 * with 1us resolution. Variables are initialized in kwp2k_init().
 */
/* Inter byte time in ECU response */
static unsigned long kwp2k_p1;

/*
 * Time between end of tester request and start of ECU response,
 * or time between end of ECU response and start of next ECU response.
 */
static unsigned long kwp2k_p2;

/*
 * Time between end of ECU response and and start of new tester request,
 * or time between end of tester request and start of new tester request
 * if ECU fails to respond. Shall be measured from the last byte in the
 * latest response message from any ECU responding.
 */
static unsigned long kwp2k_p3;
/* P4 - inter byte time in tester request. */
static unsigned long kwp2k_p4;

/* Timers */
/* measures P1 time parameter */
static rtl_timer_t *kwp2k_p1_timer;
/* measures P2 time parameter */
static rtl_timer_t *kwp2k_p2_timer;
/* measures P3 time parameter */
static rtl_timer_t *kwp2k_p3_timer;
/* measures P4 time parameter */
static rtl_timer_t *kwp2k_p4_timer;
/*
 * Monitors if communication with ECU will need to be re-initialized
 * due to communication line being idle for too long (for longer than
 * KWP2K_T_P3_DEFMAX).
 */
static rtl_timer_t *kwp2k_comm_wdog_timer;

/* lower layer state machine */
static int kwp2k_ll_st;

/* upper layer state machine */
static int kwp2k_ul_st;
/* upper layer state machine - one level stack */
static int kwp2k_ul_st_stack;

/* local functions */

/*
 * Transition lower layer state machine to another state.
 */
static int
kwp2k_ll_goto(int new_state)
{
	kwp2k_ll_st = new_state;

	return 0;
}

/*
 * Unrecoverable error occurred in lower layer receiver. Set error code
 * and abort. Let machine enter idle state.
 */
static int
kwp2k_ll_err_abort(int err_code)
{
	kwp2k_ll_comm.error = err_code;

	/* Transition state machine into idle state */
	kwp2k_ll_st = KWP_STLL_IDLE;

	/* Signal to upper layer that the job is finished */
	kwp2k_ll_comm.fl_rx_done = TRUE;

	return 0;
}

/*
 * Transition upper layer state machine to another state.
 */
static int
kwp2k_ul_goto(int new_state)
{
	kwp2k_ul_st = new_state;

	return 0;
}

/*
 * Transition upper layer state machine to "subroutine". Save
 * return state on stack.
 */
static int
kwp2k_ul_gosub(int sub_state, int return_state)
{
	kwp2k_ul_st_stack = return_state;
	kwp2k_ul_st = sub_state;

	return 0;
}

/*
 * Return from "subroutine".
 */
static int
kwp2k_ul_return(void)
{
	kwp2k_ul_st = kwp2k_ul_st_stack;

	return 0;
}

/*
 * Unrecoverable error occurred in upper layer. Set error code and abort.
 * Let machine enter idle state.
 */
static int
kwp2k_ul_err_abort(int err_code)
{
	/*
	 * In most cases, problem in upper layer originates in lower
	 * layer. Thus, capture error code from lower layer as well.
	 */
	kwp2k_ul_comm.error = (err_code << 8) | kwp2k_ll_comm.error;

	/* Signal to upper layer that the job is finished */
	kwp2k_ul_comm.fl_rx_done = TRUE;

	/* Transition state machine into idle state */
	kwp2k_ul_goto(KWP_STUL_IDLE);

	return 0;
}

/*
 * Calculates checksum byte for given data payload.
 */
static unsigned char
kwp2k_calc_chsum(unsigned char *data, int ln)
{
	int i;
	unsigned char chsum;

	for (i = 0, chsum = 0; i < ln; i++)
		chsum += data[i];

	return chsum;
}

/*
 * Assemble raw KWP2000 packet containing given data payload with SId
 * provided as the first data byte and let state machine take care
 * of sending the resulting packet to ECU.
 *
 * KWP2000 - message structure
 * --------------------------------             -------
 * | Fmt | Tgt | Src | Len | SId | ... data ...  | CS |
 * --------------------------------             -------
 *
 * Format Byte
 * msb                   lsb
 * -------------------------
 * |A1|A0|L5|L4|L3|L2|L1|L0|
 * -------------------------
 *
 * hdr mode A1 A0   description
 *    2      1  0 Header w/ address information, physical target address
 *    3      1  1 Header w/ address information, functional target address
 *
 *
 * L5-L0 length of data field of the message including SId (Service
 * Identification byte excluding Checksum byte
 *
 * Checksum byte
 *  - simple 8-bit sum of all bytes in the message, excluding CS itself.
 */
static int
kwp2k_encode_packet(unsigned char *data, int ln)
{
	unsigned char i = 0;
	unsigned char j;
	BOOL add_ln_byte = FALSE;

	/* max length of data payload is 255 bytes */
	if (ln > 255)
		return EINVAL;

	/*
	 * If there are more than 63 bytes of data, length needs to be sent
	 * in separate length byte.
	 */
	if (ln > 63)
		add_ln_byte = TRUE;

	/* fill in header part */
	/*
	 * Format byte
	 *  - header with address information, functional target address
	 *  - encodes length of data - range of 1..63 length can
	 *    be encoded in format byte. For longer messages (up to 255
	 *    data bytes), set length field to 0 (zero) and send length
	 *    information in separate length byte.
	 */
	kwp2k_ll_comm.tx_buf[i++] = add_ln_byte ? KWP2K_HDR_MODE_HM3 :
	    KWP2K_HDR_MODE_HM3 | ln;

	/* ECU address */
	kwp2k_ll_comm.tx_buf[i++] = kwp2k_params.ecu_address;

	/* tester address */
	kwp2k_ll_comm.tx_buf[i++] = kwp2k_params.tester_address;

	/* separate length byte - only if needed */
	if (add_ln_byte)
		kwp2k_ll_comm.tx_buf[i++] = ln;

	/* add data payload */
	for (j = 0; j < ln; j++)
		kwp2k_ll_comm.tx_buf[i++] = data[j];

	/* calculate and add checksum byte */
	kwp2k_ll_comm.tx_buf[i] = kwp2k_calc_chsum(kwp2k_ll_comm.tx_buf, i);

	/* finalize buffer to be sent out */
	kwp2k_ll_comm.tx_index = 0;
	kwp2k_ll_comm.tx_ln = i + 1;

	return 0;
}

/*
 * Extract data payload from raw packet received from ECU.
 */
static int
kwp2k_decode_packet(unsigned char *buf, int buf_size, int *ln)
{
	int i, s;

	/*
	 * Determine first byte of data payload.
	 * Account for "separate length byte".
	 */
	s = (kwp2k_ll_comm.rx_buf[0] & KWP2K_HDR_LEN_MSK) != 0 ? 3 : 4;

	/*
	 * Size of data payload. Subtract size of header (see above)
	 * and size of checksum (1 byte).
	 */
	*ln = kwp2k_ll_comm.rx_ln - s - 1;

	/*
	 * Packet is too short. Actually, that should not happen, as that
	 * should have been captured by lower layer, but catch this
	 * just to be on a safe side.
	 */
	if (*ln < 0) {
		kwp2k_ll_comm.error = KWP2K_LLERR_DECODE;
		return EINVAL;
	}

	/* Transfer data payload into supplied buffer */
	for (i = 0; s < kwp2k_ll_comm.rx_ln - 1 && i < buf_size; s++, i++)
		buf[i] = kwp2k_ll_comm.rx_buf[s];

	return 0;
}

/*
 * Set communication speed (baudrate)
 */
static int
kwp2k_set_comm_speed(int speed)
{
	U1BRG = (SYS_FREQ / 16 / speed - 1);

	return 0;
}

/* public functions */

/*
 * Initialize KWP2000 communication channel
 *  - UART1 is used to communicate with ECU
 */
int
kwp2k_init(void)
{
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
	kwp2k_set_comm_speed(KWP2K_BAUDRATE_NORMAL);
	U1MODE = UART_EN;

	/* TX pin is inverted, so configure it as active in LOW */
	U1STA = (UART_RX_ENABLE | UART_TX_ENABLE | UART_TX_INV);

	/* Initialize timing parameters */
	/* Max inter byte time in ECU response. */
	kwp2k_p1 = KWP2K_T_P1_DEFMAX;

	/*
	 * P2 - Time between end of tester request and start of ECU response,
	 * or time between end of ECU response and start of next ECU response.
	 */
	kwp2k_p2 = KWP2K_T_P2_DEFMAX;

	/*
	 * P3 - time between end of ECU response and start of new
	 * tester request
	 */
	kwp2k_p3 = KWP2K_T_P3_DEFMIN;

	/* P4 - inter byte time in tester request */
	kwp2k_p4 = KWP2K_T_P4_DEFMIN;

	/* Initialize timers */
	kwp2k_p1_timer = NULL;
	rtl_timer_init(&kwp2k_p1_timer, 0);

	kwp2k_p2_timer = NULL;
	rtl_timer_init(&kwp2k_p2_timer, 0);

	kwp2k_p3_timer = NULL;
	rtl_timer_init(&kwp2k_p3_timer, 0);

	kwp2k_p4_timer = NULL;
	rtl_timer_init(&kwp2k_p4_timer, 0);

	kwp2k_comm_wdog_timer = NULL;
	rtl_timer_init(&kwp2k_comm_wdog_timer, 0);

	/* communication with ECU is not established */
	kwp2k_comm_established = FALSE;
	
	/* communication parameters */
	/* ECU address - functional */
	kwp2k_params.ecu_address = 0x33;
	/* tester address - physical */
	kwp2k_params.tester_address = 0xf1;

	/* lower layer communication object */
	/* rx buffer empty */
	kwp2k_ll_comm.rx_index = 0;
	kwp2k_ll_comm.rx_ln = 0;
	kwp2k_ll_comm.fl_rx_done = FALSE;

	/* tx buffer ready */
	kwp2k_ll_comm.tx_index = 0;
	/* number of characters to be sent */
	kwp2k_ll_comm.tx_ln = 0;
	/* flag indicating that packet is being sent to ECU */
	kwp2k_ll_comm.fl_send_packet = FALSE;

	/* no error */
	kwp2k_ll_comm.error = 0;

	/* upper layer communication object */
	/* rx buffer empty */
	kwp2k_ul_comm.rx_index = 0;
	kwp2k_ul_comm.rx_ln = 0;
	kwp2k_ul_comm.fl_rx_done = FALSE;

	/* tx buffer ready */
	kwp2k_ul_comm.tx_index = 0;
	/* number of characters to be sent */
	kwp2k_ul_comm.tx_ln = 0;
	/* flag indicating that packet is being sent to ECU */
	kwp2k_ul_comm.fl_send_packet = FALSE;

	/* no error */
	kwp2k_ul_comm.error = 0;

	/* operate in "enhanced" communication mode by default */
//	kwp2k_comm_mode = KWP2K_MODE_BASIC;
//	kwp2k_comm_mode = KWP2K_MODE_DEBUG;
	kwp2k_comm_mode = KWP2K_MODE_ENHANCED;

	/* lower layer state machine is IDLE */
	kwp2k_ll_goto(KWP_STLL_IDLE);

	/* upper layer state machine is IDLE */
	kwp2k_ul_goto(KWP_STUL_IDLE);

	return 0;
}

/*
 * Returns TRUE if module operates in basic communication mode.
 * Otherwise return FALSE.
 */
BOOL
kwp2k_is_basic_mode(void)
{
	return kwp2k_comm_mode == KWP2K_MODE_BASIC;
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

	return 0;
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

	return 0;
}

/*
 * Receive character from ECU. Save receive errors if there are any.
 * Return EAGAIN if buffer is empty.
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

		return EAGAIN;
	}

	/*
	 * There is a character in receive buffer, return it along
	 * with related status error bits. Need to get error bits first -
	 * they are lost once character is pulled from buffer.
	 */
	k2p2k_ch->sts = U1STA & 0x000E;
	k2p2k_ch->ch = U1RXREG;

	return 0;
}


/*
 * Returns data received from ECU
 */
int
kwp2k_ul_recv(unsigned char **data, int *ln, int *err)
{
	/* wait either for data or error condition */
	if (!kwp2k_ul_comm.fl_rx_done)
		return EAGAIN;

	/* delete flag */
	kwp2k_ul_comm.fl_rx_done = FALSE;

	/* error occurred */
	if (kwp2k_ul_comm.error) {
		*ln = 0;
		*data = NULL;
		*err = kwp2k_ul_comm.error;

		return EPROTO;
	}

	/* valid packet received, return reference to data payload */
	*err = 0;
	*data = kwp2k_ul_comm.rx_buf;
	*ln = kwp2k_ul_comm.rx_ln;

	return 0;
}

/*
 * Instruct upper layer there are data to be sent to ECU.
 */
int
kwp2k_ul_send(unsigned char *data, int ln)
{
	int	i;

	/*
	 * If previous packet is still being sent, return. If everything
	 * works as expected, fl_send_packet flag should be cleared before
	 * machine enters idle mode. Though if something went wrong,
	 * state machine could be transitioned into idle mode with that
	 * flag still set leading to dead lock. In such case, initiating
	 * sending of new packet is the way how to recover from that state.
	 */
	if (kwp2k_ul_comm.fl_send_packet && kwp2k_ul_st != KWP_STUL_IDLE)
		return EBUSY;

	/*
	 * Transfer data to upper layer transmit buffer
	 */
	for (i = 0; i < ln && i < sizeof (kwp2k_ul_comm.tx_buf); i++)
		kwp2k_ul_comm.tx_buf[i] = data[i];

	kwp2k_ul_comm.tx_ln = ln;

	/* clear error code */
	kwp2k_ul_comm.error = 0;
	kwp2k_ul_comm.fl_rx_done = FALSE;

	/*
	 * Signal to state machine that it should commence transmission
	 * process.
	 */
	kwp2k_ul_comm.fl_send_packet = TRUE;

	/*
	 * If needed, wake-up state machine.
	 */
	if (kwp2k_ul_st == KWP_STUL_IDLE) {
		if (kwp2k_comm_established) {
			kwp2k_ul_goto(KWP_STUL_SEND);
		} else {
			/* Initialize communication first. */
			kwp2k_ul_goto(KWP_STUL_COMM_INIT);
		}
	}

	return 0;
}

/*
 * Receives raw KWP2000 packet from ECU.
 * Returns:
 *   ETIME - ECU didn't respond
 */
int
kwp2k_ll_recv(unsigned char *buf, int buf_size, int *ln, BOOL decode)
{
	int i;

	/* packet still being received */
	if (!kwp2k_ll_comm.fl_rx_done)
		return EAGAIN;

	/* packet received, clear flag */
	kwp2k_ll_comm.fl_rx_done = FALSE;

	if (kwp2k_ll_comm.error != 0)
		/* Error occurred during receiving process */
		return EPROTO;

	/*
	 * Valid packet received, transfer it to supplied buffer.
	 * Decode packet if desired (extract just data payload).
	 */
	if (decode) {
		return kwp2k_decode_packet(buf, buf_size, ln);
	} else {
		for (i = 0; i < kwp2k_ll_comm.rx_ln && i < buf_size; i++)
			buf[i] = kwp2k_ll_comm.rx_buf[i];

		*ln = i;
	}

	return 0;
}

/*
 * Send raw KWP2000 packet to ECU. Optionally send Wake up pattern and/or
 * encode packet (add header and checksum.
 */
int
kwp2k_ll_send(unsigned char *data, int ln, BOOL send_wup, BOOL encode)
{
	kwp2k_rx_char_t ch;

	/*
	 * Machine currently busy either with sending or receiving packet.
	 */
	if (kwp2k_ll_st != KWP_STLL_IDLE &&
	    kwp2k_ll_st != KWP_STLL_RX_WAIT4PKT)
		return EBUSY;

	/* If there is nothing to be sent, return */
	if (ln == 0)
		return 0;

	/*
	 * If only data payload has been provided, add necessary service
	 * bytes to assemble complete KWP2000 packet. Otherwise, just
	 * directly transfer packet to transmit buffer.
	 */
	if (encode) {
		/*
		 * encodes packet. As a side effect, initializes other transmit
		 * parameters:
		 *  - kwp2k_ll_comm.tx_ln
		 *  - kwp2k_ll_comm.tx_index
		 */
		kwp2k_encode_packet(data, ln);
	} else {
		int	i;

		for (i = 0; i < ln && i < sizeof (kwp2k_ll_comm.tx_buf); i++)
			kwp2k_ll_comm.tx_buf[i] = data[i];

		/* initialize other transmit parameters */
		/* total number of bytes to be transmitted */
		kwp2k_ll_comm.tx_ln = ln;
		/* index in transmit buffer */
		kwp2k_ll_comm.tx_index = 0;
	}

	/*
	 * Before transmission of packet is initiated, make sure receive
	 * buffer is empty. That assures lower layer receiver can recover
	 * from situations when incomplete packets are received for some reason
	 * (e.g. due to incorrect packet timing) and garbage is left
	 * unprocessed in receive buffer.
	 */
	while (kwp2k_getchar(&ch) == 0);

	/* disable receiver during transmission, echoes are discarded */
	U1STACLR = UART_RX_ENABLE;

	/* clear RX errors */
	kwp2k_ll_comm.error = 0;
	kwp2k_ll_comm.fl_rx_done = FALSE;

	if (send_wup)
		/* Start with Wake Up Pattern, then send out data. */
		kwp2k_ll_goto(KWP_STLL_TX_WUP_LOW);
	else
		/* Send out just data. */
		kwp2k_ll_goto(KWP_STLL_TX_PKT1);

	return 0;
}

/*
 * Check if transmission of packet to ECU is finished.
 */
BOOL
kwp2k_transmitter_busy(void)
{
	return (kwp2k_ll_st >= KWP_STLL_TX_WUP_LOW &&
	    kwp2k_ll_st <= KWP_STLL_TX_PKT2);
}

/*
 * Return TRUE if communication module is IDLE
 * (neither sending nor receiving packet).
 */
BOOL
kwp2k_is_idle(void)
{
	return (kwp2k_ll_st == KWP_STLL_IDLE &&
	    kwp2k_ul_st == KWP_STUL_IDLE);
}

/*
 * Return TRUE if communication with ECU is established.
 */
BOOL
kwp2k_comm_is_established(void)
{
	return kwp2k_comm_established;
}

/*
 * Set communication mode
 */
int
kwp2k_set_mode(int new_mode)
{
	int old_mode = kwp2k_comm_mode;

	if (new_mode != KWP2K_MODE_BASIC && new_mode != KWP2K_MODE_ENHANCED
	    && new_mode != KWP2K_MODE_DEBUG)
		return EINVAL;

	kwp2k_comm_mode = new_mode;

	/* adjust baudrate if leaving or entering debug mode */
	if (old_mode != new_mode &&
	    (old_mode == KWP2K_MODE_DEBUG || new_mode == KWP2K_MODE_DEBUG))
		return kwp2k_set_comm_speed(new_mode == KWP2K_MODE_DEBUG ?
		    KWP2K_BAUDRATE_DEBUG : KWP2K_BAUDRATE_NORMAL);

	return 0;
}

/*
 * Lower layer service routine
 * - sends raw packet to ECU - prepends it with WuP if needed
 * - receives raw packet from ECU
 */
static int
kwp2k_ll_service(void)
{
	kwp2k_rx_char_t ch;

	switch (kwp2k_ll_st) {
	case KWP_STLL_IDLE:
		break;

	/*
	 * Wait for 1st character of packet from ECU - must arrive within
	 * P2 time.
	 * This state is entered from KWP_STLL_TX_PKT2 one after packet
	 * from master is completely sent to ECU. It is operated with
	 * an assumption that ECU always responds to valid packet received.
	 */
	case KWP_STLL_RX_WAIT4PKT:
		/* ECU did not respond within P2 time, abort */
		if (rtl_timer_expired(kwp2k_p2_timer)) {
			kwp2k_ll_err_abort(KWP2K_LLERR_RXP2);

			break;
		}

		/* Malformed characters are discarded */
		if (kwp2k_getchar(&ch) != 0 || ch.sts != 0)
			break;

		/* Valid character received, put it to buffer. */
		kwp2k_ll_comm.rx_buf[0] = ch.ch;
		kwp2k_ll_comm.rx_index = 1;

		/* Decode total length of packet from first byte received */
		/*
		 * It might be zero, then length is sent in 4th byte called
		 * "separate length byte". In this case, length is decoded
		 * later in subsequent state KWP_STLL_RX_PKT when 4th byte
		 * arrives.
		 */
		kwp2k_ll_comm.rx_ln = ch.ch & KWP2K_HDR_LEN_MSK;

		/* Account for header and checksum */
		kwp2k_ll_comm.rx_ln += 4;

		/* Subsequent characters must arrive within P1 time */
		rtl_timer_init(&kwp2k_p1_timer, kwp2k_p1);

		kwp2k_ll_goto(KWP_STLL_RX_PKT);

		break;

	/*
	 * Receives packet from ECU.
	 * This state is entered from KWP_STLL_RX_WAIT4PKT one after
	 * 1st character from ECU is received.
	 */
	case KWP_STLL_RX_PKT:
		/* Character did not arrive within P1 time, abort */
		if (rtl_timer_expired(kwp2k_p1_timer)) {
			kwp2k_ll_err_abort(KWP2K_LLERR_RXP1);

			break;
		}

		/* Wait for character from ECU */
		if (kwp2k_getchar(&ch) != 0)
			break;

		/* If invalid character is received from ECU, abort */
		if (ch.sts != 0) {
			kwp2k_ll_err_abort(KWP2K_LLERR_BADCHAR);

			break;
		}

		/* Save character to receive buffer */
		kwp2k_ll_comm.rx_buf[kwp2k_ll_comm.rx_index++] = ch.ch;

		/*
		 * Packets with "separate length byte" have length of data
		 * payload encoded in 4th byte, account for that scenario.
		 */
		if (kwp2k_ll_comm.rx_index == 4 &&
		    (kwp2k_ll_comm.rx_buf[0] & KWP2K_HDR_LEN_MSK) == 0) {
			kwp2k_ll_comm.rx_ln += ch.ch + 1;
		}

		/* Reset P1 timer */
		rtl_timer_init(&kwp2k_p1_timer, kwp2k_p1);

		/*
		 * If it is last character, verify checksum and if
		 * packet is valid, pass it to upper layer.
		 */
		if (kwp2k_ll_comm.rx_index >= kwp2k_ll_comm.rx_ln) {
			unsigned char chsum;

			chsum = kwp2k_calc_chsum(kwp2k_ll_comm.rx_buf,
			    kwp2k_ll_comm.rx_ln - 1);

			if (chsum != ch.ch) {
				/* invalid checksum, abort */
				kwp2k_ll_err_abort(KWP2K_LLERR_CHSUM);
			} else {
				/*
				 * Valid packet received, clear error
				 * and set appropriate flag.
				 */
				kwp2k_ll_comm.error = 0;
				kwp2k_ll_comm.fl_rx_done = TRUE;

				/*
				 * Reset timer which, if expires, forces
				 * communication with ECU to be re-initialized.
				 */
				rtl_timer_init(&kwp2k_comm_wdog_timer,
				    KWP2K_T_P3_DEFMAX);

				/*
				 * Initialize P3 timer measuring min time
				 * before tester can initiate transmission
				 * of another packet after last byte of ECU
				 * response has been received.
				 */
				rtl_timer_init(&kwp2k_p3_timer, kwp2k_p3);

				/*
				 * Response might contain more that one packet,
				 * so reset P2 timer measuring max time we wait
				 * for packet from ECU and wait for that
				 * another packet.
				 */
				rtl_timer_init(&kwp2k_p2_timer, kwp2k_p2);
				kwp2k_ll_goto(KWP_STLL_RX_WAIT4PKT);
			}
		}

		break;

	/* send Wake up Pattern - low pulse */
	case KWP_STLL_TX_WUP_LOW:
		/*
		 * Wait until line is idle for desired amount of time
		 * P3 - Time between end of ECU response and start
		 *      of new tester request, or time between end of tester
		 *      request and start of new tester request if ECU fails
		 *      to respond.
		 */
		if (!rtl_timer_expired(kwp2k_p3_timer))
			break;

		/* activate Kline - that also disables UART */
		kwp2k_set_kline(TRUE);

		/* activate timer - will measure 1st half length of WuP */
		rtl_timer_init(&kwp2k_p4_timer, KWP2K_T_WUP / 2);

		kwp2k_ll_goto(KWP_STLL_TX_WUP_HIGH);
		break;

	/* send Wake up Pattern - high pulse */
	case KWP_STLL_TX_WUP_HIGH:
		/* wait until sending of low pulse is done */
		if (!rtl_timer_expired(kwp2k_p4_timer))
			break;

		/* deactivate Kline - that also enables UART */
		kwp2k_set_kline(FALSE);

		/* activate timer - will measure 2nd half of WuP */
		rtl_timer_init(&kwp2k_p4_timer, KWP2K_T_WUP / 2);

		kwp2k_ll_goto(KWP_STLL_TX_PKT1);
		break;

	/* sending data to ECU */
	case KWP_STLL_TX_PKT1:
		/*
		 * Wait until line is idle for desired amount of time
		 * P4 - Inter byte time in tester request.
		 * P3 - Time between end of ECU response and start
		 *      of new tester request, or time between end of tester
		 *      request and start of new tester request if ECU fails
		 *      to respond.
		 */
		if (!rtl_timer_expired(kwp2k_p4_timer) ||
		    !rtl_timer_expired(kwp2k_p3_timer))
			break;

		/* Send character from buffer */
		U1TXREG = kwp2k_ll_comm.tx_buf[kwp2k_ll_comm.tx_index];

		/* wait until byte it is transmitted */		
		kwp2k_ll_goto(KWP_STLL_TX_PKT2);

		break;

	/* wait until transmission of byte is finished */
	case KWP_STLL_TX_PKT2:
		if (!U1STAbits.TRMT)
			break;

		/*
		 * Line has to be silent for a while before next byte
		 * can be sent out. Initialize P4 timer to measure that time.
		 */
		rtl_timer_init(&kwp2k_p4_timer, kwp2k_p4);

		/* Move to the next byte */
		kwp2k_ll_comm.tx_index++;
		kwp2k_ll_comm.tx_ln--;

		/* Whole packet has been sent out */
		if (kwp2k_ll_comm.tx_ln <= 0) {
			/*
			 * Transmission is finished, so re-enable receiver.
			 */
			U1STASET = UART_RX_ENABLE;

			/*
			 * In enhanced mode, after packet is transmitted,
			 * to ECU, ECU is expected to send a response.
			 * Thus, wait for that response.
			 *
			 * In basic mode, received characters are processed
			 * by master_comm module and directly sent to master.
			 * Thus, switch to idle mode.
			 */
			if (kwp2k_is_basic_mode()) {
				kwp2k_ll_goto(KWP_STLL_IDLE);
			} else {
				kwp2k_ll_goto(KWP_STLL_RX_WAIT4PKT);

				/*
				 * Initialize timers measuring time from
				 * "last byte sent" event.
				 *  o P2 - measures max time we wait
				 *         for ECU response
				 *  o P3 - measures min time after tester
				 *         can send out next packet
				 */
				rtl_timer_init(&kwp2k_p2_timer, kwp2k_p2);
				rtl_timer_init(&kwp2k_p3_timer, kwp2k_p3);
			}
		} else {
			kwp2k_ll_goto(KWP_STLL_TX_PKT1);
		}

		break;
	}

	return 0;
}

/*
 * Higher layer service routine
 *  - sends packet to ECU - If needed, initialize communication first.
 *  - receives packets from ECU
 */
static int
kwp2k_ul_service(void)
{
	/* startCommunication packet */
	static const unsigned char
	    init_pkt[1] = {KWP2K_SID_START_COMM};

	/* number of packets received in ECU response */
	static int ecu_response_packet_counter = 0;

	int ret;

	switch (kwp2k_ul_st) {
	/* machine IDLE */
	case KWP_STUL_IDLE:
		break;

	/*
	 * Wait for response from ECU.
	 */
	case KWP_STUL_RECV:
		/*
		 * There is a packet to be transmitted to ECU, interrupt
		 * receiving process and commence transmission one.
		 */
		if (kwp2k_ul_comm.fl_send_packet) {
			if (kwp2k_comm_established) {
				kwp2k_ul_goto(KWP_STUL_SEND);
			} else {
				/* Initialize communication first. */
				kwp2k_ul_goto(KWP_STUL_COMM_INIT);
			}

			break;
		}

		/*
		 * Previous packet (either valid or malformed)
		 * still being processed.
		 */
		if (kwp2k_ul_comm.fl_rx_done)
			break;

		/*
		 * Receive packet from ECU, decode it and move
		 * data payload into receive buffer.
		 */
		ret = kwp2k_ll_recv(kwp2k_ul_comm.rx_buf,
		    sizeof (kwp2k_ul_comm.rx_buf), &kwp2k_ul_comm.rx_ln, TRUE);

		/* packet not received yet, try again later */
		if (ret == EAGAIN)
			break;

		/*
		 * Valid packet received. Make it available to the master.
		 */
		if (ret == 0) {
			/* Increment counter of packets received. */
			ecu_response_packet_counter++;

			kwp2k_ul_comm.error = 0;
			kwp2k_ul_comm.fl_rx_done = TRUE;

			break;
		}

		/*
		 * If lower layer signals timeout when waiting for
		 * packet from ECU and if at least one packet has been
		 * already received, then treat that condition
		 * as indicator of "response completed". In such case,
		 * do not report error and switch to idle mode.
		 */
		if (ecu_response_packet_counter > 0 &&
		    kwp2k_ll_comm.error == KWP2K_LLERR_RXP2) {
			kwp2k_ul_goto(KWP_STUL_IDLE);

			break;
		}

		/* Error occurred, signal it to master and abort */		
		kwp2k_ul_err_abort(KWP2K_ULERR_RXLL);

		break;

	/* Initialize communication with ECU */
	case KWP_STUL_COMM_INIT:
		/*
		 * Let lower layer assemble startCommunication packet and
		 * send it out along with WuP.
		 */
		kwp2k_ll_send((unsigned char *)init_pkt, 1, TRUE, TRUE);

		/*
		 * Wait for packet to be transmitted,
		 * then wait for response from ECU.
		 */
		kwp2k_ul_gosub(KWP_STUL_WAIT4TXDONE,
		    KWP_STUL_COMM_INIT_RESP);

		break;

	/* Wait for response to init packet from ECU */
	case KWP_STUL_COMM_INIT_RESP:
		/*
		 * Receive for packet from ECU, decode it and move
		 * data payload into receive buffer.
		 */
		ret = kwp2k_ll_recv(kwp2k_ul_comm.rx_buf,
		    sizeof (kwp2k_ul_comm.rx_buf), &kwp2k_ul_comm.rx_ln, TRUE);

		/* packet still being received, try again later */
		if (ret == EAGAIN)
			break;

		/* error in lower layer occurred, abort */
		if (ret != 0) {
			kwp2k_ul_err_abort(KWP2K_ULERR_RXLL);

			break;
		}

		/*
		 * Invalid response to startCommunication, abort.
		 */
		if (kwp2k_ul_comm.rx_buf[0] != (KWP2K_SID_START_COMM | 0x40)) {
			kwp2k_ul_err_abort(KWP2K_ULERR_INITRESP);

			break;
		}

		/* Valid response received. */

		/* communication has been established */
		kwp2k_comm_established = TRUE;

		/* reset related timer */
		rtl_timer_init(&kwp2k_comm_wdog_timer, KWP2K_T_P3_DEFMAX);

		/* continue with sending data to ECU */
		kwp2k_ul_goto(KWP_STUL_SEND);

		break;

	/* Send packet to ECU */
	case KWP_STUL_SEND:
		/*
		 * Reset counter of number of packets received from ECU
		 * during one response.
	 	 */
		ecu_response_packet_counter = 0;

		/*
		 * Send data to ECU. Do not send out WuP. Let lower layer
		 * assemble KWP2000 packet from given data.
		 */
		kwp2k_ll_send(kwp2k_ul_comm.tx_buf, kwp2k_ul_comm.tx_ln,
		    FALSE, TRUE);

		/*
		 * wait for packet to be transmitted,
		 * then wait for response from ECU
		 */
		kwp2k_ul_gosub(KWP_STUL_WAIT4TXDONE, KWP_STUL_RECV);

		break;

	/* wait until packet is completely transmitted to ECU */
	case KWP_STUL_WAIT4TXDONE:
		if (kwp2k_transmitter_busy())
			break;

		/* packet transmitted, clear related flag */
		kwp2k_ul_comm.fl_send_packet = FALSE;

		/* Return control to state saved on stack */
		kwp2k_ul_return();
		break;
	}

	return 0;
}

/*
 * Main service routine
 * - handles KWP2000 protocol
 */
int
kwp2k_service(void)
{
	/*
	 * If communication line is in idle state too long,
	 * then communication will need to be re-initialized next time
	 * when tester wants to communicate with ECU.
	 */
	if (kwp2k_comm_established &&
	    rtl_timer_expired(kwp2k_comm_wdog_timer))
		kwp2k_comm_established = FALSE;

	/* Lower communication layer */
	kwp2k_ll_service();
	/* Upper communication layer */
	kwp2k_ul_service();

	return 0;
}

