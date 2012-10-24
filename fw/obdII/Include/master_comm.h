#ifndef __MCOMM_H
#define __MCOMM_H

/* return codes */
#define MCOM_OK			0	/* operation succeeded */
/* packet being transmitted to master */
#define MCOM_TX_IN_PROGRESS	-1	

/* commands */
#define MCOM_CMD_GET_AD		'A'	/* get value from AD converter */
#define MCOM_CMD_SEND		'S'	/* send KWP2000 packet to K line */
/* send KWP2000 packet prepended with WuP to K line */
#define MCOM_CMD_SEND_WUP	'W'
/* Quit the application, jump to bootloader */
#define MCOM_CMD_QUIT		'q'
/* Go to sleep */
#define MCOM_CMD_SLEEP		's'

/* responses */
#define MCOM_RESP_CHAR	'r'	/* character received from K line */
#define MCOM_RESP_RXERR	'e'	/* malformed character received from K line */
#define MCOM_RESP_ADC	'a'	/* result of A/D conversion */
#define MCOM_RESP_ACK	'A'	/* ACK */
#define MCOM_RESP_NAK	'N'	/* NAK */

/* NAK errors */
#define MCOM_NAK_UNKNOWN_CMD	0x01	/* unknown command */
#define MCOM_NAK_KWP2K_TX_ERR	0x02	/* Failed to send packet to ECU */
#define MCOM_NAK_ADC_ERR	0x03	/* Failed to get data from ADC */

/* initialize master communication module */
extern int mcom_init(void);

/* Check if suspend is required. */
extern BOOL mcom_suspend(void);

extern int mcom_putchar(unsigned char ch);
extern int mcom_getchar(unsigned char *ch);

/* callback function called when character from master is received */
extern int mcom_rx_cb(unsigned char ch);
/* service routine */
extern int mcom_service(void);
#endif /* __MCOMM_H */
