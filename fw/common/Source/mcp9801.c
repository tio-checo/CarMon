/*********************************************************************
 *
 * Support for MCP9801 I2C temperature sensor. Periodically reads ambient
 * temperature and passes it to the master on request.
 *
 *********************************************************************/

#include <ctype.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>

#include "HardwareProfile.h"
#include "xc.h"

#include "master_comm.h"
#include "mcp9801.h"
#include "rt_light.h"

/* constants */
/* I2C communication pins */
#define MCP9801_SDA		0	/* RF0 */
#define MCP9801_SDA_MASK	(1 << MCP9801_SDA)
#define MCP9801_SCLK		1	/* RF1 */
#define MCP9801_SCLK_MASK	(1 << MCP9801_SCLK)

/* I2C address of MCP9801 chip */
#define MCP9801_I2C_ADDRESS	0x4F

/* addresses of MCP9801 registers */
#define MCP9801_REG_TA		0	/* ambient temperature register */
#define MCP9801_REG_CONF	1	/* configuration register */
#define MCP9801_REG_THYST	2	/* temperature hysteresis register */
#define MCP9801_REG_TSET	3	/* temperature limit-set register */

/* 500 ms temperature acquisition period. */
#define MCP9801_ACQ_PERIOD	500000UL

/* macros */
#define MCP9801_SDA_SET		LATFSET = MCP9801_SDA_MASK
#define MCP9801_SDA_CLR		LATFCLR = MCP9801_SDA_MASK
#define MCP9801_SCLK_SET	LATFSET = MCP9801_SCLK_MASK
#define MCP9801_SCLK_CLR	LATFCLR = MCP9801_SCLK_MASK

/* local types */

/* local variables */
/* state machine handling software I2C communication - lower layer */
static enum {
	MCP9801_I2C_LL_ST_IDLE = 0,	/* state machine idle */

	MCP9801_I2C_LL_ST_TXSTART,	/* Initiate transmit process */
	MCP9801_I2C_LL_ST_TXL,		/* SCLK __/-- */
	MCP9801_I2C_LL_ST_TXD,		/* move data bit to SDA */
	MCP9801_I2C_LL_ST_TXH,		/* SCLK --\__ */

	MCP9801_I2C_LL_ST_ACKL,		/* receive ACK - low clk */
	MCP9801_I2C_LL_ST_ACKR,		/* receive ACK - release bus */
	MCP9801_I2C_LL_ST_ACKH,		/* receive ACK - high clk */
	MCP9801_I2C_LL_ST_ACKD,		/* receive ACK - read data */

	MCP9801_I2C_LL_ST_RXSTART,	/* Initiate receive process */
	MCP9801_I2C_LL_ST_RXL,		/* SCLK --\__ */
	MCP9801_I2C_LL_ST_RXR,		/* release bus */
	MCP9801_I2C_LL_ST_RXH,		/* SCLK __/-- */
	MCP9801_I2C_LL_ST_RXD,		/* receive data */

	MCP9801_I2C_LL_ST_RXACKL,	/* transmit ACK/NAK - low clk */
	MCP9801_I2C_LL_ST_RXACKD,	/* transmit ACK/NAK - set SDA */
	MCP9801_I2C_LL_ST_RXACKH,	/* transmit ACK/NAK - high clk */

	MCP9801_I2C_LL_ST_STOPLC,	/* stop condition - low clk */
	MCP9801_I2C_LL_ST_STOPLD,	/* stop condition - low data */
	MCP9801_I2C_LL_ST_STOPHC,	/* stop condition - high clk */
	MCP9801_I2C_LL_ST_STOPHD	/* stop condition - high data */
} mcp9801_i2c_ll_st;

/* state machine handling software I2C communication - upper layer */
static enum {
	MCP9801_I2C_UL_ST_IDLE = 0,	/* state machine idle */
	MCP9801_I2C_UL_ST_INIT1,	/* Initialize MCP9801 chip */
	MCP9801_I2C_UL_ST_INIT2,
	MCP9801_I2C_UL_ST_INIT3,
	MCP9801_I2C_UL_ST_INIT4,
	MCP9801_I2C_UL_ST_INIT5,

	MCP9801_I2C_UL_ST_GETTEMP1,	/* Get measured temperature */
	MCP9801_I2C_UL_ST_GETTEMP2,
	MCP9801_I2C_UL_ST_GETTEMP3,
	MCP9801_I2C_UL_ST_GETTEMP4
} mcp9801_i2c_ul_st;

/* lower layer data buffer */
static unsigned int mcp9801_i2c_ll_data;
/* lower layer bit counter */
static unsigned int mcp9801_i2c_ll_bits;
/* ACK bit */
static unsigned int mcp9801_i2c_ll_ack;
/* If TRUE, lower layer generates start condition before sending out data */
static BOOL mcp9801_i2c_ll_send_start;
/* If TRUE, lower layer generates stop condition after data are sent out */
static BOOL mcp9801_i2c_ll_send_stop;
/* Set to TRUE by lower layer, once receiver has available data */
static BOOL mcp9801_i2c_ll_rx_data_ready;

/* upper layer data buffer - holds measured temperature */
static unsigned int mcp9801_temperature;

/* measures temperature acquisition period */
static rtl_timer_t *mcp9801_acq_timer;

/* local functions */

/*
 * Returns TRUE if I2C bus is busy sending or receiving word.
 */
static BOOL
mcp9801_i2c_bus_busy(void)
{
	return (mcp9801_i2c_ll_st != MCP9801_I2C_LL_ST_IDLE);
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
mcp9801_i2c_send(unsigned int data, unsigned int bits, BOOL start, BOOL stop)
{
	/* bus currently busy */
	if (mcp9801_i2c_bus_busy())
		return EBUSY;

	mcp9801_i2c_ll_data = data;
	mcp9801_i2c_ll_bits = bits;

	mcp9801_i2c_ll_send_start = start;
	mcp9801_i2c_ll_send_stop = stop;

	mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_TXSTART;

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
mcp9801_i2c_recv(unsigned int *pdata, unsigned int bits, BOOL ack, BOOL stop)
{
	/* Receive process finished, data available. */
	if (mcp9801_i2c_ll_rx_data_ready) {
		*pdata = mcp9801_i2c_ll_data;

		/* reset the flag */
		mcp9801_i2c_ll_rx_data_ready = FALSE;

		return 0;
	}

	/*
	 * If bus is currently idle, initiate receiving process.
	 * Otherwise let caller know it should try again later.
	 */
	if (!mcp9801_i2c_bus_busy()) {
		mcp9801_i2c_ll_bits = bits;
		mcp9801_i2c_ll_ack = ack;
		mcp9801_i2c_ll_send_stop = stop;

		mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_RXSTART;
	}

	return EAGAIN;
}

/*
 * Handles I2C communication with MCP9801 - lower layer.
 */
static int
mcp9801_i2c_ll_svc(void)
{
	static int d, bit_mask;

	switch (mcp9801_i2c_ll_st) {
	/* service idle */
	case MCP9801_I2C_LL_ST_IDLE:
		break;

	/*
	 * Initializes transmission process.
	 * Preconditions: SCLK is high
	 */
	case MCP9801_I2C_LL_ST_TXSTART:
		d = mcp9801_i2c_ll_data;
		bit_mask = 1 << (mcp9801_i2c_ll_bits - 1);

		/* Generate start condition if desired. */
		if (mcp9801_i2c_ll_send_start)
			MCP9801_SDA_CLR;

		mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_TXL;
		break;

	/* SCLK --\__ */
	case MCP9801_I2C_LL_ST_TXL:
		MCP9801_SCLK_CLR;

		mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_TXD;
		break;

	/* send data bit to SDA */
	case MCP9801_I2C_LL_ST_TXD:
		if (d & bit_mask)
			MCP9801_SDA_SET;
		else
			MCP9801_SDA_CLR;

		mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_TXH;
		break;

	/* SCLK __/-- */
	case MCP9801_I2C_LL_ST_TXH:
		MCP9801_SCLK_SET;

		/* Move to the next data bit. */
		bit_mask >>= 1;

		/* If all bits have been sent, move to receiving ACK */
		if (bit_mask == 0)
			mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_ACKL;
		else
			mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_TXL;

		break;

	/* receive ACK - low clk */
	case MCP9801_I2C_LL_ST_ACKL:
		MCP9801_SCLK_CLR;

		mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_ACKR;

		break;

	/* Release bus. */
	case MCP9801_I2C_LL_ST_ACKR:
		MCP9801_SDA_SET;

		mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_ACKH;
		break;

	/* Receive ACK - high clk */
	case MCP9801_I2C_LL_ST_ACKH:
		MCP9801_SCLK_SET;

		mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_ACKD;
		break;

	/* Read ACK bit */
	case MCP9801_I2C_LL_ST_ACKD:
		mcp9801_i2c_ll_ack = PORTF & MCP9801_SDA_MASK;

		/* Generate stop condition if desired. */
		if (mcp9801_i2c_ll_send_stop)
			mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_STOPLC;
		else
			mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_IDLE;

		break;

	/*
	 * Initializes receiving process.
	 * Preconditions: SCLK is high
	 */
	case MCP9801_I2C_LL_ST_RXSTART:
		d = 0;
		bit_mask = 1 << (mcp9801_i2c_ll_bits - 1);

		mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_RXL;
		break;

	/* SCLK --\__ */
	case MCP9801_I2C_LL_ST_RXL:
		MCP9801_SCLK_CLR;

		mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_RXR;
		break;

	/* Make sure that bus is release. */
	case MCP9801_I2C_LL_ST_RXR:
		MCP9801_SDA_SET;

		mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_RXH;
		break;

	/* SCLK __/-- */
	case MCP9801_I2C_LL_ST_RXH:
		MCP9801_SCLK_SET;

		mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_RXD;
		break;

	/* read SDA */
	case MCP9801_I2C_LL_ST_RXD:
		if (PORTF & MCP9801_SDA_MASK)
			d |= bit_mask;

		/* Move to the next bit. */
		bit_mask >>= 1;

		/*
		 * If all bits have been received, pass received data to upper
		 * layer and proceed with transmitting ACK/NAK.
		 */
		if (bit_mask == 0) {
			mcp9801_i2c_ll_data = d;
			mcp9801_i2c_ll_rx_data_ready = TRUE;

			mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_RXACKL;
		} else {
			mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_RXL;
		}

		break;

	/* SCLK --\__ */
	case MCP9801_I2C_LL_ST_RXACKL:
		MCP9801_SCLK_CLR;

		mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_RXACKD;
		break;

	/* transmit ACK/NAK */
	case MCP9801_I2C_LL_ST_RXACKD:
		if (mcp9801_i2c_ll_ack)
			MCP9801_SDA_CLR;
		else
			MCP9801_SDA_SET;

		mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_RXACKH;
		break;

	/* SCLK __/-- */
	case MCP9801_I2C_LL_ST_RXACKH:
		MCP9801_SCLK_SET;

		/* Continue with generating stop condition if desired. */
		if (mcp9801_i2c_ll_send_stop)
			mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_STOPLC;
		else
			mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_IDLE;
		break;

	/* Generate stop condition - low clk */
	case MCP9801_I2C_LL_ST_STOPLC:
		MCP9801_SCLK_CLR;

		mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_STOPLD;

		break;
	
	/* Generate stop condition - low data */
	case MCP9801_I2C_LL_ST_STOPLD:
		MCP9801_SDA_CLR;

		mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_STOPHC;

		break;

	/* Generate stop condition - high clk */
	case MCP9801_I2C_LL_ST_STOPHC:
		MCP9801_SCLK_SET;

		mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_STOPHD;

		break;
	
	/* Generate stop condition - high data */
	case MCP9801_I2C_LL_ST_STOPHD:
		MCP9801_SDA_SET;

		mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_IDLE;

		break;
	} /* switch */

	return 0;
}

/*
 * Handles I2C communication with MCP9801 - upper layer.
 */
static int
mcp9801_i2c_ul_svc(void)
{
	static unsigned int tmsb, tlsb;

	switch (mcp9801_i2c_ul_st) {
	/* service idle */
	case MCP9801_I2C_UL_ST_IDLE:
		break;

	/*
	 * MCP9801 chip initialization - phase 1
	 *
	 * Address MCP9801 chip - RW bit is zero (write operation).
	 * Let lower layer generate I2C start condition.
	 */
	case MCP9801_I2C_UL_ST_INIT1:
		/* Make sure that I2C bus is idle */
		if (mcp9801_i2c_bus_busy())
			break;

		mcp9801_i2c_send((MCP9801_I2C_ADDRESS << 1) | 0, 8, TRUE,
		    FALSE);

		mcp9801_i2c_ul_st = MCP9801_I2C_UL_ST_INIT2;
		break;

	/*
	 * MCP9801 chip initialization - phase 2
	 *
	 * Set MCP9801 register pointer to address of configuration register.
	 */
	case MCP9801_I2C_UL_ST_INIT2:
		/* Make sure that I2C bus is idle */
		if (mcp9801_i2c_bus_busy())
			break;

		mcp9801_i2c_send(MCP9801_REG_CONF, 8, FALSE, FALSE);

		/* Proceed with periodic reading of temperature. */
		mcp9801_i2c_ul_st = MCP9801_I2C_UL_ST_INIT3;
		break;

	/*
	 * MCP9801 chip initialization - phase 3
	 *
	 * Write MCP9801 configuration register:
	 *  - one-shot disabled (default)
	 *  - 12-bit ADC resolution (default is 9-bit)
	 *  - 1 fault queue bit (default)
	 *  - active-low alert (default)
	 *  - comparator mode (default)
	 *  - shutdown disable (default)
	 *
	 * Let lower layer generate I2C stop condition.
	 */
	case MCP9801_I2C_UL_ST_INIT3:
		/* Make sure that I2C bus is idle */
		if (mcp9801_i2c_bus_busy())
			break;

		mcp9801_i2c_send(0x60, 8, FALSE, TRUE);

		/* Proceed with periodic reading of temperature. */
		mcp9801_i2c_ul_st = MCP9801_I2C_UL_ST_INIT4;
		break;

	/*
	 * MCP9801 chip initialization - phase 4
	 *
	 * Address MCP9801 chip - RW bit is zero (write operation).
	 * Let lower layer generate I2C start condition.
	 */
	case MCP9801_I2C_UL_ST_INIT4:
		/* Make sure that I2C bus is idle */
		if (mcp9801_i2c_bus_busy())
			break;

		mcp9801_i2c_send((MCP9801_I2C_ADDRESS << 1) | 0, 8, TRUE,
		    FALSE);

		mcp9801_i2c_ul_st = MCP9801_I2C_UL_ST_INIT5;
		break;

	/*
	 * MCP9801 chip initialization - phase 5
	 *
	 * Set MCP9801 register pointer to address of temperature register,
	 * so that subsequent reads from the chip return measured temperature.
	 * Let lower layer generate I2C stop condition.
	 */
	case MCP9801_I2C_UL_ST_INIT5:
		/* Make sure that I2C bus is idle */
		if (mcp9801_i2c_bus_busy())
			break;

		mcp9801_i2c_send(MCP9801_REG_TA, 8, FALSE, TRUE);

		/* Proceed with periodic reading of temperature. */
		mcp9801_i2c_ul_st = MCP9801_I2C_UL_ST_GETTEMP1;
		break;

	/*
	 * Get measured temperature.
	 *
	 * Address MCP9801 chip - RW bit is one (read operation).
	 * Let lower layer generate I2C start condition.
	 */
	case MCP9801_I2C_UL_ST_GETTEMP1:
		/* Make sure that I2C bus is idle */
		if (mcp9801_i2c_bus_busy())
			break;

		/* Init timer measuring acquisition period. */
		rtl_timer_init(&mcp9801_acq_timer, MCP9801_ACQ_PERIOD);

		mcp9801_i2c_send((MCP9801_I2C_ADDRESS << 1) | 1, 8, TRUE,
		    FALSE);

		mcp9801_i2c_ul_st = MCP9801_I2C_UL_ST_GETTEMP2;
		break;

	/* Obtain MSB byte. */
	case MCP9801_I2C_UL_ST_GETTEMP2:
		/* Wait until byte is received */
		/* Instruct lower layer to generate ACK. */
		if (mcp9801_i2c_recv(&tmsb, 8, TRUE, FALSE) != 0)
			break;

		mcp9801_i2c_ul_st = MCP9801_I2C_UL_ST_GETTEMP3;
		break;

	/* Obtain LSB byte. */
	case MCP9801_I2C_UL_ST_GETTEMP3:
		/* Wait until byte is received. */
		/* Instruct lower layer to generate NAK and stop condition. */
		if (mcp9801_i2c_recv(&tlsb, 8, FALSE, TRUE) != 0)
			break;

		/* Move obtained temperature to resulting buffer. */
		mcp9801_temperature = tmsb << 8 | tlsb;

		mcp9801_i2c_ul_st = MCP9801_I2C_UL_ST_GETTEMP4;
		break;

	/* Wait for a while, then start another acquistion cycle. */
	case MCP9801_I2C_UL_ST_GETTEMP4:
		if (rtl_timer_expired(mcp9801_acq_timer)) {
			mcp9801_i2c_ul_st = MCP9801_I2C_UL_ST_GETTEMP1;
		}

		break;
	} /* switch */

	return 0;
}

/*
 * Main service routine - called periodically from timer interrupt.
 */
static int
mcp9801_svc(void)
{
	/* Handle communication with MCP9801 - lower layer */
	mcp9801_i2c_ll_svc();

	/* Handle communication with MCP9801 - upper layer */
	mcp9801_i2c_ul_svc();

	return 0;
}

/* global functions */
/*
 * Initialization routine.
 */
int
mcp9801_init(void)
{
	/*
	 * initialize SDA and SCLK I2C pins - open collectors.
	 */
	LATFSET = MCP9801_SDA_MASK | MCP9801_SCLK_MASK;
	ODCFSET = MCP9801_SDA_MASK | MCP9801_SCLK_MASK;
	TRISFCLR = MCP9801_SDA_MASK | MCP9801_SCLK_MASK;

	/* Initialize I2C communication state machines. */
	/* Lower layer is idle. */
	mcp9801_i2c_ll_st = MCP9801_I2C_LL_ST_IDLE;
	/* Let upper layer initialize MCP9801 chip . */
	mcp9801_i2c_ul_st = MCP9801_I2C_UL_ST_INIT1;

	mcp9801_temperature = 0x55aa;

	mcp9801_i2c_ll_rx_data_ready = FALSE;

	/* Initialize timer measuring acquisition period. */
	mcp9801_acq_timer = NULL;
	rtl_timer_init(&mcp9801_acq_timer, 0);

	/* Register and enable MCP9801 service. */
	rtl_svc_init("mcp9801", mcp9801_svc, TRUE, FALSE);

	return 0;
}

/*
 * Get last measured temperature.
 */
int
mcp9801_get_temperature(void)
{
	return (mcp9801_temperature);
}
