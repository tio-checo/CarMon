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

/* I2C communication pins */
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

/* I2C communication pins */
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
/* I2C message */
typedef struct i2csw_msg_s {
	unsigned char addr;	/* device address */
	unsigned char *buf;	/* pointer to data buffer */
	int ln;		/* # of bytes to be read or written */
} i2csw_msg_t;

/* local variables */
/* TRUE if I2C bus is currently granted to consumer. */
static BOOL i2csw_bus_granted;

/* I2C message */
static i2csw_msg_t i2csw_msg;

/* state machine handling software I2C communication - lower layer */
static enum {
	I2CSW_I2C_LL_ST_IDLE = 0,	/* state machine idle */

	I2CSW_I2C_LL_ST_TXSTART,	/* Initiate transmit process */
	I2CSW_I2C_LL_ST_TXL,		/* SCLK __/-- */
	I2CSW_I2C_LL_ST_TXD,		/* move data bit to SDA */
	I2CSW_I2C_LL_ST_TXH,		/* SCLK --\__ */

	I2CSW_I2C_LL_ST_ACKL,		/* receive ACK - low clk */
	I2CSW_I2C_LL_ST_ACKR,		/* receive ACK - release bus */
	I2CSW_I2C_LL_ST_ACKH,		/* receive ACK - high clk */
	I2CSW_I2C_LL_ST_ACKD,		/* receive ACK - read data */

	I2CSW_I2C_LL_ST_RXSTART,	/* Initiate receive process */
	I2CSW_I2C_LL_ST_RXL,		/* SCLK --\__ */
	I2CSW_I2C_LL_ST_RXR,		/* release bus */
	I2CSW_I2C_LL_ST_RXH,		/* SCLK __/-- */
	I2CSW_I2C_LL_ST_RXD,		/* receive data */

	I2CSW_I2C_LL_ST_RXACKL,	/* transmit ACK/NAK - low clk */
	I2CSW_I2C_LL_ST_RXACKD,	/* transmit ACK/NAK - set SDA */
	I2CSW_I2C_LL_ST_RXACKH,	/* transmit ACK/NAK - high clk */

	I2CSW_I2C_LL_ST_STOPLC,	/* stop condition - low clk */
	I2CSW_I2C_LL_ST_STOPLD,	/* stop condition - low data */
	I2CSW_I2C_LL_ST_STOPHC,	/* stop condition - high clk */
	I2CSW_I2C_LL_ST_STOPHD	/* stop condition - high data */
} i2csw_i2c_ll_st;

/* state machine handling software I2C communication - upper layer */
static enum {
	I2CSW_I2C_UL_ST_IDLE = 0,	/* state machine idle */
	I2CSW_I2C_UL_ST_WRITE1,		/* Write bytes to device */
	I2CSW_I2C_UL_ST_WRITE2,
	I2CSW_I2C_UL_ST_WRITE3,

	I2CSW_I2C_UL_ST_READ1,		/* Read bytes from device */
	I2CSW_I2C_UL_ST_READ2
} i2csw_i2c_ul_st;

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

/*
 * Returns TRUE if I2C bus is busy sending or receiving word.
 */
static BOOL
i2csw_i2c_bus_busy(void)
{
	return (i2csw_i2c_ll_st != I2CSW_I2C_LL_ST_IDLE);
}

/*
 * Returns TRUE if I2C upper layer is busy reading from or writing to device.
 */
static BOOL
i2csw_i2c_ul_busy(void)
{
	return (i2csw_i2c_ul_st != I2CSW_I2C_UL_ST_IDLE);
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
static int
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
static int
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
static int
i2csw_i2c_ll_svc(void)
{
	static int bit_mask;
	static unsigned char d;

	switch (i2csw_i2c_ll_st) {
	/* service idle */
	case I2CSW_I2C_LL_ST_IDLE:
		break;

	/*
	 * Initializes transmission process.
	 * Preconditions: SCLK is high
	 */
	case I2CSW_I2C_LL_ST_TXSTART:
		d = i2csw_i2c_ll_data;
		bit_mask = 1 << (i2csw_i2c_ll_bits - 1);

		/* Generate start condition if desired. */
		if (i2csw_i2c_ll_send_start)
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
		i2csw_i2c_ll_ack = PORTF & I2CSW_SDA_MASK;

		/* Generate stop condition if desired. */
		if (i2csw_i2c_ll_send_stop)
			i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_STOPLC;
		else
			i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_IDLE;

		break;

	/*
	 * Initializes receiving process.
	 * Preconditions: SCLK is high
	 */
	case I2CSW_I2C_LL_ST_RXSTART:
		d = 0;
		bit_mask = 1 << (i2csw_i2c_ll_bits - 1);

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_RXL;
		break;

	/* SCLK --\__ */
	case I2CSW_I2C_LL_ST_RXL:
		I2CSW_SCLK_CLR;

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_RXR;
		break;

	/* Make sure that bus is release. */
	case I2CSW_I2C_LL_ST_RXR:
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
		if (PORTF & I2CSW_SDA_MASK)
			d |= bit_mask;

		/* Move to the next bit. */
		bit_mask >>= 1;

		/*
		 * If all bits have been received, pass received data to upper
		 * layer and proceed with transmitting ACK/NAK.
		 */
		if (bit_mask == 0) {
			i2csw_i2c_ll_data = d;
			i2csw_i2c_ll_rx_data_ready = TRUE;

			i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_RXACKL;
		} else {
			i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_RXL;
		}

		break;

	/* SCLK --\__ */
	case I2CSW_I2C_LL_ST_RXACKL:
		I2CSW_SCLK_CLR;

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_RXACKD;
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

		/* Continue with generating stop condition if desired. */
		if (i2csw_i2c_ll_send_stop)
			i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_STOPLC;
		else
			i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_IDLE;
		break;

	/* Generate stop condition - low clk */
	case I2CSW_I2C_LL_ST_STOPLC:
		I2CSW_SCLK_CLR;

		i2csw_i2c_ll_st = I2CSW_I2C_LL_ST_STOPLD;

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
 * Handles I2C communication with I2CSW - upper layer.
 */
static int
i2csw_i2c_ul_svc(void)
{
	static unsigned int cnt;

	switch (i2csw_i2c_ul_st) {
	/* service idle */
	case I2CSW_I2C_UL_ST_IDLE:
		break;

	/*
	 * Write to device - phase 1
	 *
	 * Address I2C device - RW bit is zero (write operation).
	 * Let lower layer generate I2C start condition.
	 */
	case I2CSW_I2C_UL_ST_WRITE1:
		/* Make sure that I2C bus is idle */
		if (i2csw_i2c_bus_busy())
			break;

		i2csw_i2c_send((i2csw_msg.addr << 1) | 0, 8, TRUE,
		    FALSE);

		/* Initialize byte counter. */
		cnt = 0;

		i2csw_i2c_ul_st = I2CSW_I2C_UL_ST_WRITE2;
		break;

	/*
	 * Write to device - phase 2
	 *
	 * Write data byte to the device.
	 */
	case I2CSW_I2C_UL_ST_WRITE2: {
		BOOL last;

		/* Make sure that I2C bus is idle */
		if (i2csw_i2c_bus_busy())
			break;

		last = (cnt + 1 == i2csw_msg.ln);

		/* If it is the last byte, generate stop condition. */
		i2csw_i2c_send(i2csw_msg.buf[cnt], 8, FALSE, last);

		/* Increment byte counter - used as index to data buffer */
		cnt++;

		/* All bytes written. */
		if (last) {
			i2csw_i2c_ul_st = I2CSW_I2C_UL_ST_WRITE3;
		}
	} break;

	/*
	 * Write to device - phase 3
	 *
	 * Wait until lower layer finishes its job.
	 */
	case I2CSW_I2C_UL_ST_WRITE3:
		/* Wait until I2C bus is idle */
		if (i2csw_i2c_bus_busy())
			break;

		i2csw_i2c_ul_st = I2CSW_I2C_UL_ST_IDLE;
		break;

	/*
	 * Read bytes from device - phase 1.
	 *
	 * Address the device - RW bit is one (read operation).
	 * Let lower layer generate I2C start condition.
	 */
	case I2CSW_I2C_UL_ST_READ1:
		/* Make sure that I2C bus is idle */
		if (i2csw_i2c_bus_busy())
			break;

		i2csw_i2c_send((i2csw_msg.addr << 1) | 1, 8, TRUE,
		    FALSE);

		/* Initialize byte counter. */
		cnt = 0;

		i2csw_i2c_ul_st = I2CSW_I2C_UL_ST_READ2;
		break;

	/* Read one byte from the device. */
	case I2CSW_I2C_UL_ST_READ2: {
		BOOL last = (cnt + 1 == i2csw_msg.ln);

		/*
		 * Wait until byte is received.
		 *
		 * If this is the last byte to be received,
		 * instruct lower layer to generate NAK and stop condition.
		 * Otherwise instruct lower layer to generate ACK which
		 * indicates to device that another byte is to be received.
		 */
		if (i2csw_i2c_recv(&i2csw_msg.buf[cnt], 8, !last, last) != 0)
			break;

		/* Increment byte counter - used as index to data buffer */
		cnt++;

		/* All bytes read, go to idle state. */
		if (last) {
			i2csw_i2c_ul_st = I2CSW_I2C_UL_ST_IDLE;
		}

	} break;
	} /* switch */

	return 0;
}

/*
 * Main service routine - called periodically from timer interrupt.
 */
static int
i2csw_svc(void)
{
	/* Handle communication with I2CSW - lower layer */
	i2csw_i2c_ll_svc();

	/* Handle communication with I2CSW - upper layer */
	i2csw_i2c_ul_svc();

	return 0;
}

/* global functions */
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
	/* Upper layer is idle. */
	i2csw_i2c_ul_st = I2CSW_I2C_UL_ST_IDLE;

	i2csw_i2c_ll_rx_data_ready = FALSE;

	/* Bus is available. */
	i2csw_bus_granted = FALSE;

	/* Register and enable I2CSW service. */
	rtl_svc_init("i2csw", i2csw_svc, TRUE, FALSE);

	return 0;
}

/*
 * Grants I2C bus to a consumer. Must be called by consumer before
 * communication with I2C device is commenced. That assures that
 * only one device accesses I2C bus at a time.
 *
 * Returns 0 if bus was granted, otherwise returns EBUSY.
 */
int
i2csw_request_bus(void)
{
	if (i2csw_bus_granted)
		return EBUSY;

	i2csw_bus_granted = TRUE;

	return 0;
}

/*
 * Releases I2C bus. Must be called by consumer after the consumer
 * is done with communication.
 *
 * Returns 0.
 */
int
i2csw_release_bus(void)
{
	i2csw_bus_granted = FALSE;

	return 0;
}

/*
 * Writes data to I2C device.
 *
 * It is a non-blocking function to be called periodically until
 * communication layer finishes writing.
 *
 * Parameters:
 *  addr - I2C address of the device
 *  data - pointer to data buffer
 *  ln   - number of bytes to be read from the device
 *
 * Return values:
 *  0      - Writing finished
 *  EPERM  - Bus not granted to the consumer
 *  ENXIO  - No device responded to specified address
 *  EIO    - Communication error
 *  EAGAIN - Writing in progress
 */
int
i2csw_write(unsigned char addr, unsigned char *data, int ln)
{
	static BOOL write_in_progress = FALSE;

	/* Bus is not granted, do not commence an operation. */
	if (!i2csw_bus_granted)
		return EPERM;

	/* If operation is in progress, check if it finished. */
	if (write_in_progress) {
		/* Operation not finished yet, return. */
		if (i2csw_i2c_ul_busy())
			return EAGAIN;

		/* Operation finished */
		write_in_progress = FALSE;

		return 0;
	}

	/* New writing should be commenced. */
	/* It is no-op if there are no bytes to be written. */
	if (ln <= 0)
		return 0;

	/* Prepare I2C message */
	i2csw_msg.addr = addr;
	i2csw_msg.buf = data;
	i2csw_msg.ln = ln;

	/* Kick off writing process. */
	i2csw_i2c_ul_st = I2CSW_I2C_UL_ST_WRITE1;
	write_in_progress = TRUE;

	return EAGAIN;
}

/*
 * Reads data from I2C device.
 * It is a non-blocking function to be called periodically until
 * communication layer finishes reading.
 *
 * Parameters:
 *  addr - I2C address of the device
 *  data - pointer to data buffer
 *  ln   - number of bytes to be read from the device
 *
 * Return values:
 *  0      - reading finished, buffer contains valid data
 *  EPERM  - Bus not granted to the consumer
 *  ENXIO  - No device responded to specified address
 *  EIO    - Communication error
 *  EAGAIN - reading now in progress
 */
int
i2csw_read(unsigned char addr, unsigned char *data, int ln)
{
	static BOOL read_in_progress = FALSE;

	/* Bus is not granted, do not commence an operation. */
	if (!i2csw_bus_granted)
		return EPERM;

	/* If operation is in progress, check if it finished. */
	if (read_in_progress) {
		/* Operation not finished yet, return. */
		if (i2csw_i2c_ul_busy())
			return EAGAIN;

		/* Operation finished */
		read_in_progress = FALSE;

		return 0;
	}

	/* New reading should be commenced. */
	/* It is no-op if there are no bytes to be read. */
	if (ln <= 0)
		return 0;

	/* Prepare I2C message */
	i2csw_msg.addr = addr;
	i2csw_msg.buf = data;
	i2csw_msg.ln = ln;

	/* Kick off reading process. */
	i2csw_i2c_ul_st = I2CSW_I2C_UL_ST_READ1;
	read_in_progress = TRUE;

	return EAGAIN;
}

