#ifndef __MON_H
#define __MON_H

/* return codes */
#define MON_OK		0	/* operation succeeded */
#define MON_RX_EMPTY	-1	/* receive buffer empty */
#define MON_TX_FULL	-2	/* transmit buffer full */

/* initialize monitor module */
extern int mon_init(void);

extern int mon_putchar(unsigned char ch);
extern int mon_getchar(unsigned char *ch, int debug);

/* service routine */
extern int mon_service(void);
#endif /* __MON_H */
