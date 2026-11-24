#include <stdio.h>
/*********************************************************************
 *
 * Support for 24Cxx I2C EEPROM.
 *
 * Memory is divided into two independent regions. The first one contains
 * user options/parameters, the second one keeps internal non-volatile
 * state of carmon service.
 * Each part is protected by its own CRC. That assures that even
 * if one part is corrupted, the another one remains usable.
 * To speed up write process, Page Write operation is used to store
 * data to EEPROM. Start as well as size of both regions must
 * be selected appropriately to account for that.
 *
 *********************************************************************/

#include <ctype.h>
#include <errno.h>
#include <sys/time.h>
#include <string.h>
#include <time.h>

#include "master_comm.h"
#include "i2c_hal.h"
#include "ee24.h"
#include "rt_light.h"
#include "utils.h"

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* constants */
/* I2C address of 24Cxx EEPROM chip */
#define EE24_I2C_ADDRESS	0x54

/* Page Write page size */
#define EE24_PAGE_SIZE		16

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

/* macros */

/* local types */

/* local variables */
/* EEPROM device on the shared I2C bus. */
static i2c_master_dev_handle_t ee24_dev;

/* EEPROM read to RWM */
static BOOL ee24_copied2ram;

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

/* local functions */

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
	i2c_master_bus_handle_t bus;
	i2c_device_config_t config = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = EE24_I2C_ADDRESS,
		.scl_speed_hz = 100000,
	};
	unsigned char address = 0;
	int err;

	ee24_param_crc_valid = FALSE;
	ee24_nv_crc_valid = FALSE;
	ee24_copied2ram = FALSE;

	/* Initialize pointers to the in-memory copy. */
	ee24_param_list = ee24_ram_copy;
	ee24_nv_state = ee24_ram_copy + EE24_NV_OFFSET;

	/* Attach EEPROM to the previously initialized I2C bus. */
	err = i2c_master_get_bus_handle(I2C_NUM_0, &bus);
	if (err != ESP_OK)
		return (err);

	err = i2c_master_bus_add_device(bus, &config, &ee24_dev);
	if (err != ESP_OK)
		return (err);

	/* Read both regions, including their CRC, in one transaction. */
	err = i2c_master_transmit_receive(ee24_dev,
	    &address, sizeof (address),
	    ee24_ram_copy, sizeof (ee24_ram_copy), 100);
	if (err != ESP_OK)
		goto fail;

	/* Validate the two regions independently. */
	ee24_param_crc_valid =
	    (crc_dma_calc_array(ee24_param_list, EE24_PAR_SIZE) == 0);
	ee24_nv_crc_valid =
	    (crc_dma_calc_array(ee24_nv_state, EE24_NV_SIZE) == 0);

	ee24_copied2ram = TRUE;

	return (0);

fail:
	(void)i2c_master_bus_rm_device(ee24_dev);
	ee24_dev = NULL;
	return (err);
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
	int offset, length, remaining;

	/* Invalid EEPROM content or parameter number. */
	if (num < 1 || num > EE24_NUM_PARS || size == NULL || pvalue == NULL ||
	    !ee24_initialized() || !ee24_params_valid()) {
		return EINVAL;
	}

	offset = ee24_getpar_offset(num);

	if (offset < 0 || offset >= EE24_PAR_SIZE - 2) {
		return EINVAL;
	}

	length = ee24_param_list[offset];

	/* Reserve at least a length byte for each following parameter. */
	remaining = EE24_NUM_PARS - num;
	if (length > EE24_PAR_SIZE - 2 - offset - 1 - remaining)
		return (EINVAL);

	*size = length;
	*pvalue = &ee24_param_list[offset + 1];

	return (0);
}

/*
 * Set EEPROM parameter synchronously.
 * Must be called from a task, never from an interrupt.
 *
 * Return values:
 *       0 - parameter written and read back successfully
 *  EINVAL - invalid parameter or value
 *  ENODEV - EEPROM not initialized
 *     EIO - write or verification failed
 */
int
ee24_setpar(int num, int ln, char *value)
{
	unsigned char pending[EE24_PAR_SIZE];
	unsigned char verify[EE24_PAR_SIZE];
	unsigned char page[EE24_PAGE_SIZE + 1];
	unsigned char address = EE24_PAR_OFFSET;
	unsigned short crc;
	int offset, size, index, remaining;
	esp_err_t err;

	if (!ee24_initialized())
		return (ENODEV);

	if (num < 1 || num > EE24_NUM_PARS ||
	    value == NULL || ln <= 0 || (ln % 2) != 0)
		return (EINVAL);

	/*
	 * If EEPROM content is invalid, only the first parameter
	 * can be written. Start with an empty parameter region.
	 */
	if (!ee24_params_valid()) {
		if (num != 1)
			return (EINVAL);

		memset(pending, 0, sizeof (pending));
	} else {
		memcpy(pending, ee24_param_list, sizeof (pending));
	}

	offset = ee24_getpar_offset(num);
	size = ln / 2;

	/* The length byte must be inside the parameter region. */
	if (offset < 0 || offset >= EE24_PAR_SIZE - 2)
		return (EINVAL);

	/*
	 * Reserve the CRC and at least a length byte for each
	 * following parameter.
	 */
	remaining = EE24_NUM_PARS - num;
	if (size > EE24_PAR_SIZE - 2 - offset - 1 - remaining)
		return (EINVAL);

	/* Decode into the working copy, leaving current data untouched. */
	if (hex2ucharbuf(value, &pending[offset + 1], ln) != 0)
		return (EINVAL);

	pending[offset] = size;

	/* Calculate CRC, then store CRC most significant byte first. */
	crc = crc_dma_calc_array(pending, sizeof (pending) - 2);
	pending[EE24_PAR_SIZE - 2] = crc >> 8;
	pending[EE24_PAR_SIZE - 1] = crc & 0xff;

	/* Write the last page first, as in the original implementation. */
	for (index = EE24_PAR_SIZE - EE24_PAGE_SIZE; index >= 0;
	    index -= EE24_PAGE_SIZE) {
		page[0] = EE24_PAR_OFFSET + index;
		memcpy(page + 1, pending + index, EE24_PAGE_SIZE);

		err = i2c_master_transmit(ee24_dev, page, sizeof (page),
		    100);
		if (err != ESP_OK)
			goto fail;

		/* Wait for internal programming, including tick phase. */
		vTaskDelay(pdMS_TO_TICKS(10) + 1);
	}

	/* Verify the actual EEPROM contents before reporting success. */
	err = i2c_master_transmit_receive(ee24_dev,
	    &address, sizeof (address), verify, sizeof (verify), 100);
	if (err != ESP_OK ||
	    memcmp(pending, verify, sizeof (pending)) != 0)
		goto fail;

	memcpy(ee24_param_list, pending, sizeof (pending));
	ee24_param_crc_valid = TRUE;

	return (0);

fail:
	/* A failed operation may have written only part of the region. */
	ee24_param_crc_valid = FALSE;
	return (EIO);
}

/*
 * Store nv state of carmon service to EEPROM synchronously.
 * Must be called from a task, never from an interrupt.
 * EEPROM access and modifications of its RAM copy must be serialized
 * by the caller for the entire operation, including programming delays.
 *
 * Return values:
 *       0 - nv state written without reported I2C errors
 *  ENODEV - EEPROM not initialized
 *     EIO - I2C write failed
 */
int
ee24_save_nv_state(void)
{
	unsigned char address;
	unsigned short crc;
	int index;
	esp_err_t err;
	i2c_master_transmit_multi_buffer_info_t buffers[2] = {
		{
			.write_buffer = &address,
			.buffer_size = sizeof (address)
		},
		{
			.buffer_size = EE24_PAGE_SIZE
		}
	};

	if (!ee24_initialized())
		return (ENODEV);

	/* Calculate CRC directly in the RAM copy, MSB first. */
	crc = crc_dma_calc_array(ee24_nv_state, EE24_NV_SIZE - 2);
	ee24_nv_state[EE24_NV_SIZE - 2] = crc >> 8;
	ee24_nv_state[EE24_NV_SIZE - 1] = crc & 0xff;

	/* Write the last page first, as in the original implementation. */
	for (index = EE24_NV_SIZE - EE24_PAGE_SIZE; index >= 0;
	    index -= EE24_PAGE_SIZE) {
		address = EE24_NV_OFFSET + index;
		buffers[1].write_buffer = ee24_nv_state + index;

		/* Send the EEPROM address followed by one page of data. */
		err = i2c_master_multi_buffer_transmit(ee24_dev, buffers,
		    sizeof (buffers) / sizeof (buffers[0]), 100);
		if (err != ESP_OK)
			goto fail;

		/* Wait for internal programming, including tick phase. */
		vTaskDelay(pdMS_TO_TICKS(10) + 1);
	}

	ee24_nv_crc_valid = TRUE;

	return (0);

fail:
	/* A failed operation may have written only part of the region. */
	ee24_nv_crc_valid = FALSE;
	return (EIO);
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
