#ifndef __KWP2K_H
#define __KWP2K_H

#include "GenericTypeDefs.h"
#include "xc.h"

/* return codes */
#define KWP2K_OK		0	/* operation succeeded */
#define KWP2K_RX_EMPTY		-1	/* No characters received from ECU */
/* transmitter currently busy with sending packet to ECU */
#define KWP2K_TX_BUSY		-2

/* status bits accompanying character received from master */
/* receive buffer overrun */
#define KWP2K_RXSTS_OERR	0x02
/* frame error - likely because WuP pattern was detected */
#define KWP2K_RXSTS_FERR	0x04
/* parity error */
#define KWP2K_RXSTS_PERR	0x08

/*
 * Various timing aspects of KWP2000 packet - values are in microseconds.
 */
/* Length of Wake up Pattern */
#define KWP2K_T_WUP		50000
/* Inter byte time in tester request. */
#define KWP2K_T_P4_DEFMIN	5000	/* default min */
#define KWP2K_T_P4_DEFMAX	20000	/* default max */

/* macros */

/* types */
typedef struct kwp2k_rx_char {
	unsigned char	ch;
	unsigned long	sts;
} kwp2k_rx_char_t;

/* functions */
/* initialize KWP2000 communication module */
extern int kwp2k_init(void);

/* Suspend the module. */
extern int kwp2k_suspend(void);

/* return character received from ECU */
extern int kwp2k_getchar(kwp2k_rx_char_t *k2p2k_ch);

/*
 * Send KWP2000 packet to ECU. Optionally prepend it with Wake up pattern.
 */
extern int kwp2k_send_packet(unsigned char *tx_buff, int ln, BOOL send_wup);

/*
 * Check if transmission of packet to ECU is finished.
 */
extern BOOL kwp2k_transmitter_busy(void);


/* service routine */
extern int kwp2k_service(void);
#endif /* __KWP2K_H */
