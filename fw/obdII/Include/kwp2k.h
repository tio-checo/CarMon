/*********************************************************************
 *
 * Handles communication with ECU using KWP2000 protocol. Implements Data
 * Link Layer (OSI layer 2) as specified in following ISO 14230-2 document:
 *
 *  http://www.alfa145.co.uk/obd/14230-2s.pdf
 *
 *********************************************************************/

#ifndef __KWP2K_H
#define __KWP2K_H

#include "GenericTypeDefs.h"
#include "xc.h"

/* KWP communication modes */
#define KWP2K_MODE_BASIC	1	/* basic mode */
#define KWP2K_MODE_ENHANCED	2	/* enhanced mode */
#define KWP2K_MODE_DEBUG	3	/* debug mode */

/* status bits accompanying character received from master */
/* receive buffer overrun */
#define KWP2K_RXSTS_OERR	0x02
/* frame error - likely because WuP pattern was detected */
#define KWP2K_RXSTS_FERR	0x04
/* parity error */
#define KWP2K_RXSTS_PERR	0x08

/*
 * Definitions related to KWP2000 message structure
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
 */
#define KWP2K_HDR_MODE_HM2	0x80
#define KWP2K_HDR_MODE_HM3	0xc0
#define KWP2K_HDR_A1_MSK	0x80
#define KWP2K_HDR_A0_MSK	0x40
#define KWP2K_HDR_LEN_MSK	0x3f

/*
 * Various timing aspects of KWP2000 packet - values are in microseconds.
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
 * default values for P1-P4 timing parameters
 *
 * Value min max description
 *--------------------------
 * P1    0   20  Inter byte time in ECU response
 * P2   25   50  Time between end of tester request and start of ECU response
 * P3   55  5000 Time between end of ECU response and start of new tester
 *               request
 * P4    5   20  Inter byte time in tester request
 */
#define KWP2K_T_P1_DEFMIN	0	/* P1 default min */
#define KWP2K_T_P1_DEFMAX	20000	/* P1 default max */
#define KWP2K_T_P2_DEFMIN	25000	/* P2 default min */
#define KWP2K_T_P2_DEFMAX	50000	/* P2 default max */
#define KWP2K_T_P3_DEFMIN	55000	/* P3 default min */
#define KWP2K_T_P3_DEFMAX	5000000	/* P3 default max */
#define KWP2K_T_P4_DEFMIN	5000	/* P4 default min */
#define KWP2K_T_P4_DEFMAX	20000	/* P4 default max */
/*
 * Wake up Pattern - part of "fast initialization" sequence
 *
 * <-line idle-> <-------WuP-------> <--packet from tester----> <-line idle ->
 * -------------|          |--------| |-|----| |----|-| |----|||--------------
 *              |__________|        |_| |    |-|    | |_|    |||
 *               <- 25ms ->              <P4>   <P4>     <P4>
 *               <----- 50ms ------>
 */
#define KWP2K_T_WUP		50000	/* total lenght of WuP */

/* SIds */
#define KWP2K_SID_START_COMM		0x81	/* start communication */
#define KWP2K_SID_STOP_COMM		0x82	/* stop communication */
#define KWP2K_SID_ACCESS_TIMING_PARAMS	0x83	/* access timing parameters */

/* return codes */
/* receiver errors */
/* lower layer errors */
/* ECU didn't provide next character within expected P1 time */
#define KWP2K_LLERR_RXP1	0x01
/* ECU didn't respond within expected P2 time */
#define KWP2K_LLERR_RXP2	0x02
/* bad character received from ECU */
#define KWP2K_LLERR_BADCHAR	0x03
/* bad packet received from ECU */
#define KWP2K_LLERR_BADPKT	0x04
/* bad checksum */
#define KWP2K_LLERR_CHSUM	0x05
/* error occurred when packet was being decoded */
#define KWP2K_LLERR_DECODE	0x06

/* upper layer errors */
/* lower layer signals there was an error during packet reception */
#define KWP2K_ULERR_RXLL	0x41
/* invalid response to startCommunication packet */
#define KWP2K_ULERR_INITRESP	0x42

/* max length of raw KWP2000 packet including all service bytes */
#define KWP2K_PACKET_MAXLN	260

/* macros */

/* types */
/* character received on K line */
typedef struct kwp2k_rx_char {
	unsigned char	ch;
	unsigned long	sts;
} kwp2k_rx_char_t;

/*
 * structure encapsulating data objects used when receiving or sending data.
 */
typedef struct _kwp2l_rx_tx_t {
	/* receive buffer */
	unsigned char rx_buf[KWP2K_PACKET_MAXLN];
	/*
	 * Index of a position in receive buffer where next received
	 * character would be stored.
	 */
	int rx_index;
	/* number of characters received */
	int rx_ln;
	/*
	 * Flag indicating that receiver finished its job.
	 * Either valid packet has been received or unrecoverable
	 * error occurred.
	 */
	BOOL fl_rx_done;

	/* transmit buffer */
	unsigned char tx_buf[KWP2K_PACKET_MAXLN];
	/* Index in transmit buffer of next to-be-sent character. */
	int tx_index;
	/* number of to-be-sent characters */
	int tx_ln;
	/* Flag indicating that there is a packet to be transmitted. */
	BOOL fl_send_packet;

	/* Error occurred during last receive/transmit operation. */
	unsigned int error;
} kwp2k_rx_tx_t;

/* functions */
/* Initialize KWP2000 communication module. */
extern int kwp2k_init(void);

/* Return TRUE if module operates in basic communication mode. */
extern BOOL kwp2k_is_basic_mode(void);

/*
 * Return TRUE if communication module is IDLE
 * (neither sending nor receiving packet).
 */
extern BOOL kwp2k_is_idle(void);

/* Suspend the module. */
extern int kwp2k_suspend(void);

/* return character received from ECU */
extern int kwp2k_getchar(kwp2k_rx_char_t *k2p2k_ch);

/* Returns data received from ECU */
extern int kwp2k_ul_recv(unsigned char **, int *, int *);

/* Send data to ECU via upper layer */
extern int kwp2k_ul_send(unsigned char *, int);

/*
 * Send KWP2000 packet to ECU. Optionally send Wake up pattern
 * and/or encode packet (add header and checksum).
 */
extern int kwp2k_ll_send(unsigned char *, int, BOOL, BOOL);

/* Check if transmission of packet to ECU is finished. */
extern BOOL kwp2k_transmitter_busy(void);

/* Set communication mode */
extern int kwp2k_set_mode(int mode);

/* Return TRUE if communication with ECU is established. */
extern BOOL kwp2k_comm_is_established(void);

/* service routine */
extern int kwp2k_service(void);
#endif /* __KWP2K_H */
