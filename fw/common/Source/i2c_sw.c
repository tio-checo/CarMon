/*********************************************************************
 *
 * Software implementation of I2C bus.
 *
 *********************************************************************/

#include <errno.h>

#include "HardwareProfile.h"
#include "xc.h"

#include "i2c_sw.h"
#include "rt_light.h"

/* constants */
#ifdef RFID_MODULE

/* I2C communication port and pins */
#define I2CSW_PORT	PORTF
#define I2CSW_SDA	0	/* RF0 */
#define I2CSW_SDA_MASK	(1 << I2CSW_SDA)
#define I2CSW_SCLK	1	/* RF1 */
#define I2CSW_SCLK_MASK	(1 << I2CSW_SCLK)

/* macros */
#define I2CSW_SDA_SET	LATFSET = I2CSW_SDA_MASK
#define I2CSW_SDA_CLR	LATFCLR = I2CSW_SDA_MASK
#define I2CSW_SCLK_SET	LATFSET = I2CSW_SCLK_MASK
#define I2CSW_SCLK_CLR	LATFCLR = I2CSW_SCLK_MASK

#else

/* I2C communication port and pins */
#define I2CSW_PORT	PORTB
#define I2CSW_SDA	7	/* RB7 */
#define I2CSW_SDA_MASK	(1 << I2CSW_SDA)
#define I2CSW_SCLK	6	/* RB6 */
#define I2CSW_SCLK_MASK	(1 << I2CSW_SCLK)

/* macros */
#define I2CSW_SDA_SET	LATBSET = I2CSW_SDA_MASK
#define I2CSW_SDA_CLR	LATBCLR = I2CSW_SDA_MASK
#define I2CSW_SCLK_SET	LATBSET = I2CSW_SCLK_MASK
#define I2CSW_SCLK_CLR	LATBCLR = I2CSW_SCLK_MASK

#endif /* RFID_MODULE */

/* local types */

/* local variables */

/* state machine handling software I2C communication - lower layer */
static enum {
	I2CSW_I2C_LL_ST_IDLE = 0,	/* state machine idle */

	I2CSW_I2C_LL_ST_TXSTART,	/* Initiate transmit process */
	I2CSW_I2C_LL_ST_TXSTARTLD,	/* Start condition - SDA --\__ */
	I2CSW_I2C_LL_ST_TXL,		/* SCLK --\__ */
	I2CSW_I2C_LL_ST_TXD,		/* move data bit to SDA */
	I2CSW_I2C_LL_ST_TXH,		/* SCLK __/-- */

	I2CSW_I2C_LL_ST_ACKL,		/* receive ACK - low clk */
	I2CSW_I2C_LL_ST_ACKR,		/* receive ACK - release bus */
	I2CSW_I2C_LL_ST_ACKH,		/* receive ACK - high clk */
	I2CSW_I2C_LL_ST_ACKD,		/* receive ACK - read data */

	I2CSW_I2C_LL_ST_RXSTART,	/* Initiate receive process */
	I2CSW_I2C_LL_ST_RXH,		/* SCLK __/-- */
	I2CSW_I2C_LL_ST_RXD,		/* receive data */

	I2CSW_I2C_LL_ST_RXACKD,	/* transmit ACK/NAK - set SDA */
	I2CSW_I2C_LL_ST_RXACKH,	/* transmit ACK/NAK - high clk */
	I2CSW_I2C_LL_ST_RXACKL,	/* transmit ACK/NAK - low clk */

	I2CSW_I2C_LL_ST_STOPLD,	/* stop condition - low data */
	I2CSW_I2C_LL_ST_STOPHC,	/* stop condition - high clk */
	I2CSW_I2C_LL_ST_STOPHD	/* stop condition - high data */
} i2csw_i2c_ll_st;

/* lower layer data buffer */
static unsigned char i2csw_i2c_ll_data;
/* lower layer bit counter */
static unsigned int i2csw_i2c_ll_bits;
/* ACK bit */
static unsigned int i2csw_i2c_ll_ack;
/* If TRUE, lower layer generates start condition before sending out data */
static BOOL i2csw_i2c_ll_send_start;
/* If TRUE, lower layer generates stop condition after data are sent out */
static BOOL i2csw_i2c_ll_send_stop;
/* Set to TRUE by lower layer, once receiver has available data */
static BOOL i2csw_i2c_ll_rx_data_ready;

/* local functions */

/* global functions */
/*
 * Returns TRUE if I2C bus is busy sending or receiving word.
 */
BOOL
i2csw_i2c_bus_busy(void)
{
	return (i2csw_i2c_ll_st != I2CSW_I2C_LL_ST_IDLE);
}

/*
 * Instruct lower layer to send out data on I2C bus.
 *
 * Parameters:
 *  data  - data to be sent out starting with MSB
 *  bits  - number of bits to be sent out
 *  start - if TRUE, then I2C start condition is generated
 *  stop  - if TRUE, then I2C stop condition is generated
 *
 * Returns: EBUSY if I2C bus is currently busy, otherwise 0.
 */
int
i2csw_i2c_send(unsigned char data, unsigned int bits, BOOL start, BOOL stop)
{
	/* bus currently busy */
	if (i2csw_i2c_bus_busy())
		return EBUSY;

	i2csw_i2c_ll_data = data;
	i2csw_i2c_ll_bits = bits;

	i2csw_i2c_ll_send_start = start;
	i2csw_i2c_ll_send_stop = stop;

	i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_TXSTART;

	return 0;
}

/*
 * Instruct lower layer to receive data from I2C bus.
 *
 * Parameters:
 *  bits  - number of bits to be received
 *  pdata - pointer where to store received data
 *  ack   - if TRUE, then ACK is generated, otherwise NAK.
 *  stop  - if TRUE, then I2C stop condition is generated
 *
 * Returns: EAGAIN - receiving in progress
 *          0      - data available
 */
int
i2csw_i2c_recv(unsigned char *pdata, unsigned int bits, BOOL ack, BOOL stop)
{
	/* Receive process finished, data available. */
	if (i2csw_i2c_ll_rx_data_ready) {
		*pdata = i2csw_i2c_ll_data;

		/* reset the flag */
		i2csw_i2c_ll_rx_data_ready = FALSE;

		return 0;
	}

	/*
	 * If bus is currently idle, initiate receiving process.
	 * Otherwise let caller know it should try again later.
	 */
	if (!i2csw_i2c_bus_busy()) {
		i2csw_i2c_ll_bits = bits;
		i2csw_i2c_ll_ack = ack;
		i2csw_i2c_ll_send_stop = stop;

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_RXSTART;
	}

	return EAGAIN;
}

/*
 * Handles I2C communication with I2CSW - lower layer.
 */
int
i2csw_i2c_ll_svc(void)
{
	static int bit_mask;
	static unsigned char d;

	switch (i2csw_i2c_ll_st) {
	/* service idle */
	case I2CSW_I2C_LL_ST_IDLE:
		break;

	/*
	 * Transmitter
	 *
	 * Initial state:
	 *  - before the 1st write:     SDA high, SCL high
	 *  - before subsequent writes: SDA high, SCL low
	 *
	 * Request to generate start condition is valid
	 * in both scenarios, so account for that.
	 */
	case I2CSW_I2C_LL_ST_TXSTART:
		d = i2csw_i2c_ll_data;
		bit_mask = 1 << (i2csw_i2c_ll_bits - 1);

		/*
		 * Generate start condition if desired.
	 	 * Preconditions: SDA is high
		 */
		if (i2csw_i2c_ll_send_start) {
			/* SCLK __/-- */
			I2CSW_SCLK_SET;

			i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_TXSTARTLD;
		} else {
			/* SCLK --\__ */
			I2CSW_SCLK_CLR;
			i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_TXD;
		}
		break;

	/* SDA --\__ */ 
	case I2CSW_I2C_LL_ST_TXSTARTLD:
		I2CSW_SDA_CLR;

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_TXL;
		break;

	/* SCLK --\__ */
	case I2CSW_I2C_LL_ST_TXL:
		I2CSW_SCLK_CLR;

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_TXD;
		break;

	/* send data bit to SDA */
	case I2CSW_I2C_LL_ST_TXD:
		if (d & bit_mask)
			I2CSW_SDA_SET;
		else
			I2CSW_SDA_CLR;

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_TXH;
		break;

	/* SCLK __/-- */
	case I2CSW_I2C_LL_ST_TXH:
		I2CSW_SCLK_SET;

		/* Move to the next data bit. */
		bit_mask >>= 1;

		/* If all bits have been sent, move to receiving ACK */
		if (bit_mask == 0)
			i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_ACKL;
		else
			i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_TXL;

		break;

	/* receive ACK - low clk */
	case I2CSW_I2C_LL_ST_ACKL:
		I2CSW_SCLK_CLR;

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_ACKR;

		break;

	/* Release bus. */
	case I2CSW_I2C_LL_ST_ACKR:
		I2CSW_SDA_SET;

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_ACKH;
		break;

	/* Receive ACK - high clk */
	case I2CSW_I2C_LL_ST_ACKH:
		I2CSW_SCLK_SET;

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_ACKD;
		break;

	/* Read ACK bit */
	case I2CSW_I2C_LL_ST_ACKD:
		i2csw_i2c_ll_ack = I2CSW_PORT & I2CSW_SDA_MASK;

		/* SCLK --\__ */
		I2CSW_SCLK_CLR;

		/* Generate stop condition if desired. */
		if (i2csw_i2c_ll_send_stop)
			i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_STOPLD;
		else
			i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_IDLE;
		break;

	/*
	 * Receiver
	 *
	 * Initial state:
	 *  - after write (1st read):    SDA high, SCL low
	 *  - after read (ACK was sent): SDA low,  SCL low
	 */
	case I2CSW_I2C_LL_ST_RXSTART:
		d = 0;
		bit_mask = 1 << (i2csw_i2c_ll_bits - 1);

		/* Make sure that bus is released. */
		I2CSW_SDA_SET;

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_RXH;
		break;

	/* SCLK __/-- */
	case I2CSW_I2C_LL_ST_RXH:
		I2CSW_SCLK_SET;

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_RXD;
		break;

	/* read SDA */
	case I2CSW_I2C_LL_ST_RXD:
		if (I2CSW_PORT & I2CSW_SDA_MASK)
			d |= bit_mask;

		/* Move to the next bit. */
		bit_mask >>= 1;

		/* SCLK --\__ */
		I2CSW_SCLK_CLR;

		/*
		 * If all bits have been received, pass received data to upper
		 * layer and proceed with transmitting ACK/NAK.
		 */
		if (bit_mask == 0) {
			i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_RXACKD;
		} else {
			i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_RXH;
		}

		break;

	/* transmit ACK/NAK */
	case I2CSW_I2C_LL_ST_RXACKD:
		if (i2csw_i2c_ll_ack)
			I2CSW_SDA_CLR;
		else
			I2CSW_SDA_SET;

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_RXACKH;
		break;

	/* SCLK __/-- */
	case I2CSW_I2C_LL_ST_RXACKH:
		I2CSW_SCLK_SET;

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_RXACKL;
		break;

	/* Finish transmitting ACK/NAK */
	/* SCLK --\__ */
	case I2CSW_I2C_LL_ST_RXACKL:
		/* Make data available to upper layer */
		i2csw_i2c_ll_data = d;
		i2csw_i2c_ll_rx_data_ready = TRUE;

		I2CSW_SCLK_CLR;
		/* Continue with generating stop condition if desired. */
		if (i2csw_i2c_ll_send_stop)
			i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_STOPLD;
		else
			i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_IDLE;
		break;

	/* Generate stop condition - low data */
	case I2CSW_I2C_LL_ST_STOPLD:
		I2CSW_SDA_CLR;

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_STOPHC;

		break;

	/* Generate stop condition - high clk */
	case I2CSW_I2C_LL_ST_STOPHC:
		I2CSW_SCLK_SET;

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_STOPHD;

		break;
	
	/* Generate stop condition - high data */
	case I2CSW_I2C_LL_ST_STOPHD:
		I2CSW_SDA_SET;

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_IDLE;

		break;
	} /* switch */

	return 0;
}

/*
 * Initialization routine.
 */
int
i2csw_init(void)
{
	/*
	 * initialize SDA and SCLK I2C pins - open collectors.
	 */
#ifdef RFID_MODULE
	LATFSET = I2CSW_SDA_MASK | I2CSW_SCLK_MASK;
	ODCFSET = I2CSW_SDA_MASK | I2CSW_SCLK_MASK;
	TRISFCLR = I2CSW_SDA_MASK | I2CSW_SCLK_MASK;
#else
	LATBSET = I2CSW_SDA_MASK | I2CSW_SCLK_MASK;
	ODCBSET = I2CSW_SDA_MASK | I2CSW_SCLK_MASK;
	TRISBCLR = I2CSW_SDA_MASK | I2CSW_SCLK_MASK;
#endif

	/* Initialize I2C communication state machines. */
	/* Lower layer is idle. */
	i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_IDLE;

	i2csw_i2c_ll_rx_data_ready = FALSE;

	return 0;
}
