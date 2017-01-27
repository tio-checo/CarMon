/*********************************************************************
 *
 * Support for 24Cxx I2C EEPROM.
 *
 * Memory is divided into two independent parts. The first one contains
 * user options/parameters, the second one keeps internal non-volatile
 * state of carmon service.
 * Each part is protected by its own CRC. That assures that even
 * if one part is corrupted, the another one remains usable.
 *
 *********************************************************************/

#include <ctype.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>

#include "HardwareProfile.h"
#include "xc.h"

#include "master_comm.h"
#include "i2c_hal.h"
#include "ee24.h"
#include "rt_light.h"
#include "utils.h"

/* constants */
/* I2C address of 24Cxx EEPROM chip */
#define EE24_I2C_ADDRESS	0x54

/* offset and size of the first part containing user parameters */
#define EE24_PAR_OFFSET		0
#define EE24_PAR_SIZE		64
/* offset and size of the second part keeping non-volatile state */
#define EE24_NV_OFFSET		EE24_PAR_SIZE
#define EE24_NV_SIZE		64
/* total in-use size of EEPROM */
#define EE24_TOTAL_SIZE		(EE24_PAR_SIZE + EE24_NV_SIZE)

/* max number of parameteres stored in EEPROM */
#define EE24_NUM_PARS		2

/* Max write time per datasheet is 10ms. */
#define EE24_EEW_DELAY		10000

/* macros */

/* local types */

/* local variables */
/* EEPROM read to RWM */
static BOOL ee24_copied2ram;

/* state machine handling communication with the sensor */
static enum {
	EE24_ST_IDLE = 0,	/* state machine idle */

	EE24_ST_INIT1,		/* reads eeprom parameters into memory */
	EE24_ST_INIT2,

	EE24_ST_STORE_PAR1,	/* store parameter list to EEPROM */
	EE24_ST_STORE_NV1,	/* store nv state to EEPROM */
	EE24_ST_STORE_NVPAR2,
	EE24_ST_STORE_NVPAR3
} ee24_st;

/* TRUE if EEPROM section keeping user parameters is valid; FALSE otherwise. */
static BOOL ee24_param_crc_valid;

/* TRUE if EEPROM section keeping nv state is valid; FALSE otherwise. */
static BOOL ee24_nv_crc_valid;

/* in-memory copy of EEPROM */
static unsigned char ee24_ram_copy[EE24_TOTAL_SIZE];

/* pointer to in-memory list of user parameters */
static unsigned char *ee24_param_list;

/* pointer to in-memory non-volatile carmon service state  */
static unsigned char *ee24_nv_state;

/* Timer used by EEPROM write operation. */
static rtl_timer_t *ee24_eew_timer;

/* local functions */

/*
 * Main service routine - called periodically from timer interrupt.
 * Handles communication with 24Cxx EEPROM.
 */
static int
ee24_svc(void)
{
	static int index;
	static int ee_offset;
	static unsigned char buf[2];
	static unsigned char *data;

	switch (ee24_st) {
	/* service idle */
	case EE24_ST_IDLE:
		break;

	/*
	 * Reads EEPROM parameters and non-volatile
	 * state from the device to RAM.
	 */
	case EE24_ST_INIT1:
		/* Grab i2c bus. */
		if (i2c_request_bus() != 0) {
			break;
		}

		ee24_st = EE24_ST_INIT2;
		break;

	/* Read the whole EEPROM content to RAM. */
	case EE24_ST_INIT2: {
		unsigned short crc;
		int ms;

		if (i2c_random_read(EE24_I2C_ADDRESS, 0, ee24_ram_copy,
		    EE24_TOTAL_SIZE) == EAGAIN)
			break;

		/* Release I2C bus */
		i2c_release_bus();

		/* Validate EEPROM content - section of user parameters. */
		ms = EE24_PAR_SIZE;
		crc = crc_dma_calc_array(ee24_param_list, ms);

		ee24_param_crc_valid = (crc == 0);

		/* Validate EEPROM content - section of nv state. */
		ms = EE24_NV_SIZE;
		crc = crc_dma_calc_array(ee24_nv_state, ms);

		ee24_nv_crc_valid = (crc == 0);

		/* EEPROM content copied to RWM */
		ee24_copied2ram = TRUE;

		ee24_st = EE24_ST_IDLE;
	} break;

	/* Store parameter list to EEPROM */
	case EE24_ST_STORE_PAR1:
		/* Grab i2c bus. */
		if (i2c_request_bus() != 0) {
			break;
		}

		/* Write last byte first. */
		index = EE24_PAR_SIZE - 1;
		ee_offset = EE24_PAR_OFFSET;
		data = ee24_param_list;

		/* Byte address followed by byte itself to be written. */
		buf[0] = ee_offset + index;
		buf[1] = data[index];

		ee24_st = EE24_ST_STORE_NVPAR2;
		break;

	/* Store nv state to EEPROM */
	case EE24_ST_STORE_NV1:
		/* Grab i2c bus. */
		if (i2c_request_bus() != 0) {
			break;
		}

		/* Write last byte first. */
		index = EE24_NV_SIZE - 1;
		ee_offset = EE24_NV_OFFSET;
		data = ee24_nv_state;

		/* Byte address followed by byte itself to be written. */
		buf[0] = ee_offset + index;
		buf[1] = data[index];

		ee24_st = EE24_ST_STORE_NVPAR2;
		break;

	/* Write one byte to EEPROM */
	case EE24_ST_STORE_NVPAR2:
		if (i2c_write(EE24_I2C_ADDRESS, buf, 2) == EAGAIN) {
			break;
		}

		/* Now wait 10ms until the write is completed. */
		rtl_timer_init(&ee24_eew_timer, EE24_EEW_DELAY);

		ee24_st = EE24_ST_STORE_NVPAR3;
		break;

	/* Wait 10ms until the byte is written. */
	case EE24_ST_STORE_NVPAR3:
		if (!rtl_timer_expired(ee24_eew_timer)) {
			break;
		}

		/* Proceed with writing another byte, if there is any. */
		index--;
		if (index < 0) {
			/* Release I2C bus */
			i2c_release_bus();

			ee24_st = EE24_ST_IDLE;

			break;
		}

		buf[0] = ee_offset + index;
		buf[1] = data[index];

		ee24_st = EE24_ST_STORE_NVPAR2;
		break;
	} /* switch */

	return 0;
}

/*
 * Returns TRUE if the service is currently busy. Otherwise returns FALSE.
 */
static BOOL
ee24_svc_busy(void)
{
	return (ee24_st != EE24_ST_IDLE);
}

/*
 * Locates parameter in the list of parameters.
 * Assumes valid parameter is provided - no checks are done.
 * 
 * Return values:
 *       >=0 - offset 
 *       -1  - parameter not found
 */
static int
ee24_getpar_offset(int num)
{
	int offset, i;

	offset = 0;
	for (i = 1; i < num; i++) {
		offset += ee24_param_list[offset];

		offset++;

		if (offset > EE24_PAR_SIZE) {
			return -1;
		}
	}

	return offset;
}
 
/* global functions */
/*
 * Initialization routine.
 */
int
ee24_init(void)
{
	/* Register and enable ee24 service. */
	rtl_svc_init("ee24", ee24_svc, TRUE, FALSE);

	ee24_param_crc_valid = FALSE;
	ee24_nv_crc_valid = FALSE;

	/* Initialize EEPROM write timer. */
	ee24_eew_timer = NULL;
	rtl_timer_init(&ee24_eew_timer, 0);

	ee24_copied2ram = FALSE;

	/*
	 * Initialize pointers to in-memory
	 * copy of user parameters and nv-state.
	 */
	ee24_param_list = ee24_ram_copy;
	ee24_nv_state = ee24_ram_copy + EE24_NV_OFFSET;

	/* State machine is IDLE */
	ee24_st = EE24_ST_INIT1;

	return 0;
}

/*
 * Get EEPROM parameter.
 *
 * Return values:
 *       0 - operation finished, valid data available
 *  EINVAL - invalid parameter number
 */
int
ee24_getpar(int num, int *size, unsigned char **pvalue)
{
	int offset;

	/* Invalid EEPROM content or parameter number. */
	if (num < 1 || num > EE24_NUM_PARS || !ee24_params_valid()) {
		return EINVAL;
	}

	offset = ee24_getpar_offset(num);

	if (offset == -1) {
		return EINVAL;
	}

	*size = ee24_param_list[offset];
	*pvalue = &ee24_param_list[offset + 1];

	return 0;
}

/*
 * Set EEPROM parameter.
 * Data provided in form of HEX ASCII string.
 *
 * Return values:
 *       0 - operation commenced, data are to be written to the device
 *  EINVAL - invalid parameter number
 *  EBUSY  - module currently busy with previous operation
 */
int
ee24_setpar(int num, int ln, char *value)
{
	int offset, ms;
	unsigned short crc;

	/* Previous operation still in progress. */
	if (ee24_svc_busy()) {
		return EBUSY;
	}

	/* Check for invalid parameter number. */
	if (num < 1 || num > EE24_NUM_PARS) {
		return EINVAL;
	}

	/*
	 * If EEPROM content is invalid, then only
	 * the first parameter can be written.
	 */
	if (!ee24_params_valid() && (num != 1)) {
		return EINVAL;
	}

	/* Locate parameter in the list. */
	offset = ee24_getpar_offset(num);

	if (offset == -1) {
		return EINVAL;
	}

	/* Make sure that parameter and its size fits the available space. */
	if (ln / 2 + offset + 1 > EE24_PAR_SIZE) {
		return EINVAL;
	}

	/* Populate the parameter */
	if (hex2ucharbuf(value, &ee24_param_list[offset + 1], ln) != 0) {
		return EINVAL;
	}

	/* Store parameter's size */
	ee24_param_list[offset] = ln / 2;

	/* Recalculate CRC */
	ms = EE24_PAR_SIZE;
	/*
	 * DMA CRC calculation relies on message being appended
	 * with 16 zero bits.
	 */
	ee24_param_list[ms - 2] = ee24_param_list[ms - 1] = 0;
	crc = crc_dma_calc_array(ee24_param_list, ms);
	ee24_param_list[ms - 2] = crc >> 8;
	ee24_param_list[ms - 1] = crc & 0xff;

	/* Initiate process of storing parameter list to EEPROM */
	ee24_st = EE24_ST_STORE_PAR1;

	return 0;
}

/*
 * Store nv state of carmon service to EEPROM.
 *
 * Return values:
 *      0 - operation commenced, data are to be written to the device
 *  EBUSY - module currently busy with previous operation
 */
int
ee24_save_nv_state(void)
{
	int ms;
	unsigned short crc;

	/* Previous operation still in progress. */
	if (ee24_svc_busy()) {
		return EBUSY;
	}

	/* Recalculate CRC */
	ms = EE24_NV_SIZE;
	/*
	 * DMA CRC calculation relies on message being appended
	 * with 16 zero bits.
	 */
	ee24_nv_state[ms - 2] = ee24_nv_state[ms - 1] = 0;
	crc = crc_dma_calc_array(ee24_nv_state, ms);
	ee24_nv_state[ms - 2] = crc >> 8;
	ee24_nv_state[ms - 1] = crc & 0xff;

	/* Initiate process of storing nv state to EEPROM */
	ee24_st = EE24_ST_STORE_NV1;

	return 0;
}

/*
 * Returns pointer to in-memory copy of carmon service nv state.
 */
unsigned char *
ee24_get_nv_state(void)
{
	return ee24_nv_state;
}

/*
 * Returns TRUE if EEPROM section holding user parameters is valid;
 * FALSE otherwise.
 */
BOOL
ee24_params_valid(void)
{
	return ee24_param_crc_valid;
}

/*
 * Returns TRUE if EEPROM section keeping nv state is valid;
 * FALSE otherwise.
 */
BOOL
ee24_nv_state_valid(void)
{
	return ee24_nv_crc_valid;
}

/*
 * Returns TRUE if EEPROM content is copied to memory; FALSE otherwise. 
 */
BOOL
ee24_initialized(void)
{
	return ee24_copied2ram;
}

/*
 * Prints diagnostic information.
 */
void
ee24_print_diag_info(int (*print_str)(const char *))
{
	int i;
	char s[81];

	/* header */
	print_str("-------------------------------------\r\n");

	/* CRC status */
	snprintf(s, sizeof (s),"CRC par: %s\r\n",
	    ee24_param_crc_valid ? "valid" : "invalid");
	print_str(s);
	snprintf(s, sizeof (s),"CRC nv: %s\r\n",
	    ee24_nv_crc_valid ? "valid" : "invalid");
	print_str(s);

	/* Dump whole EEPROM */
	for (i = 0; i < EE24_TOTAL_SIZE; i++) {
		snprintf(s, sizeof (s), "%02X", ee24_ram_copy[i]);
		print_str(s);
	}
	print_str("\r\n");

	/* footer */
	print_str("-------------------------------------\r\n");
}
