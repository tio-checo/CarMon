#ifndef __MCOMM_H
#define __MCOMM_H

#include <GenericTypeDefs.h>

/* module IDs and commands */
#define	MCOMM_CAN_ID		'C'	/* CAN module ID */
#define	MCOMM_COMM_ID		'M'	/* communication module ID */
#define	MCOMM_PIC_ID		'P'	/* PIC module ID */
#define MCOMM_RFID_ID		'R'	/* RFID module ID */

/* data type in data payload sent in response to master */
#define	MCOM_DATA_TYPE_ASCII	'A'	/* raw ASCII data */
#define	MCOM_DATA_TYPE_BYTE	'B'	/* 8-bit unsigned */
#define	MCOM_DATA_TYPE_WORD	'W'	/* 16-bit unsigned */

#define	MCOMM_ECU_ID		'E'	/* ECU module ID */

/* acquisition module IDs */
#define	MCOMM_ADC_ID		'A'	/* A/D converter module */
#define	MCOMM_DAQ_ID		'Q'	/* acquisition module */

/* legacy commands */
#define MCOM_CMD_GET_AD		'A'	/* get value from AD converter */
#define MCOM_CMD_SEND		'S'	/* send KWP2000 packet to K line */
/* send KWP2000 packet prepended with WuP to K line */
#define MCOM_CMD_SEND_WUP	'W'
/* change communication mode */
#define MCOM_CMD_CHANGE_MODE	'M'
/* Interrupt the application, jump to diagnostic mode */
#define MCOM_CMD_DIAG_MODE	'D'
/* Quit the application, jump to bootloader */
#define MCOM_CMD_QUIT		'q'
/* Go to sleep */
#define MCOM_CMD_SLEEP		's'

/* responses */
#define MCOM_RESP_DATA	"r"	/* packet received from ECU */
#define MCOM_RESP_RXERR	"e"	/* malformed character received from K line */
#define MCOM_RESP_ADC	"a"	/* result of A/D conversion */
#define MCOM_RESP_ACK	"A"	/* ACK */
#define MCOM_RESP_NAK	"N"	/* NAK */

/* generic NAK errors */
#define MCOM_NAK_UNKNOWN_IDCMD	0x01	/* unknown module ID or command */
#define MCOM_NAK_EINVAL		0x02	/* invalid value */
#define MCOM_NAK_MODULE_BUSY	0x03	/* underlying module busy */

/* NAK specific to ECU - TODO move it to its own header file */
#define MCOM_NAK_KWP2K_TX_ERR	0x04	/* Failed to send packet to ECU */
#define MCOM_NAK_ADC_ERR	0x05	/* Failed to get data from ADC */
#define MCOM_NAK_ECU_TOUT	0x06	/* ECU didn't respond */
#define MCOM_NAK_ECU_BADPKT	0x07	/* bad packet received from ECU */
/* Packet with invalid checksum received from ECU */
#define MCOM_NAK_ECU_CHSUM	0x08
/* Invalid response from ECU received */
#define MCOM_NAK_ECU_INV_RESP	0x09
/* Failed to establish communication with ECU */
#define MCOM_NAK_ECU_INIT	0x0a
#define MCOM_NAK_KWP2K_BUSY	0x0b	/* Communication module is busy */
#define MCOM_NAK_ECU_UKNOWN	0xff	/* unknown error */

/* types */
/* Structure encapsulating response to master */
typedef struct mcom_response {
	char id;			/* module ID */
	char *header;			/* pointer to packet header */
	void *data;			/* pointer to data payload */
	unsigned int data_type;	/* data type */
	unsigned int data_ln;		/* data length */
} mcom_response_t;

/* initialize master communication module */
extern int mcom_init(void);

/* Prepare ACK response to master. */
extern int mcom_prepare_ack_response(mcom_response_t *);

/* Prepare NAK response to master. */
extern int mcom_prepare_nak_response(mcom_response_t *, unsigned char);

/* Prepare generic response to master. */
extern int mcom_prepare_generic_response(mcom_response_t *, char *, void *,
    int, int);

/*
 * Translate error codes from underlying modules to error
 * codes understood by master.
 */
extern int mcom_err_translate(int);

/* Queue response for transmission to master */
extern int mcom_queue_response(mcom_response_t *);

/* Check if suspend is required. */
extern BOOL mcom_suspend(void);

extern int mcom_putchar(unsigned char);
extern int mcom_getchar(unsigned char *);

/* callback function called when character from master is received */
extern int mcom_rx_cb(unsigned char);
#endif /* __MCOMM_H */
