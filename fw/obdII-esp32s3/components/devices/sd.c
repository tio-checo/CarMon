/*********************************************************************
 *
 * SDMMC storage backend for DLFS.
 * All calls must be serialized by the application task, never an ISR.
 *
 *********************************************************************/

#include <inttypes.h>
#include <string.h>

#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "sdmmc_cmd.h"

#include "carmon_media.h"

#define SD_SECTOR_SIZE	512

static sdmmc_card_t sd_card;
static bool sd_ready;
static bool sd_host_initialized;
/* Shared staging buffer keeps arbitrary caller buffers away from DMA. */
static uint8_t *sd_buffer;

/* Release only the host owned by this backend. */
static bool
sd_host_release(void)
{
	esp_err_t err;

	if (!sd_host_initialized)
		return (true);

	err = sdmmc_host_deinit();
	if (err != ESP_OK) {
		ESP_LOGW("sd", "Host cleanup failed: %s", esp_err_to_name(err));
		return (false);
	}

	sd_host_initialized = false;
	return (true);
}

/*
 * Initialize once, or retry after a failed initialization/I/O operation.
 * No filesystem is mounted and no sectors are written here.
 */
void
carmon_media_init(void)
{
	sdmmc_host_t host = SDMMC_HOST_DEFAULT();
	sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
	const char *step = "host initialization";
	esp_err_t err;

	if (sd_ready)
		return;

	/* A failed I/O operation leaves the host for this retry to release. */
	if (!sd_host_release())
		return;

	memset(&sd_card, 0, sizeof (sd_card));

	host.flags &= ~(SDMMC_HOST_FLAG_8BIT | SDMMC_HOST_FLAG_DDR);
	host.max_freq_khz = SDMMC_FREQ_DEFAULT;
/*	host.max_freq_khz = SDMMC_FREQ_PROBING; */

	/* Same wiring as hardware_test; external pull-ups are fitted. */
	slot.clk = GPIO_NUM_14;
	slot.cmd = GPIO_NUM_21;
	slot.d0 = GPIO_NUM_13;
	slot.d1 = GPIO_NUM_12;
	slot.d2 = GPIO_NUM_48;
	slot.d3 = GPIO_NUM_47;
	slot.width = 4;
	slot.cd = GPIO_NUM_NC;
	slot.wp = GPIO_NUM_NC;

	err = sdmmc_host_init();
	if (err != ESP_OK)
		goto fail;
	sd_host_initialized = true;

	step = "slot initialization";
	err = sdmmc_host_init_slot(host.slot, &slot);
	if (err != ESP_OK)
		goto fail;

	step = "card initialization";
	err = sdmmc_card_init(&host, &sd_card);
	if (err != ESP_OK)
		goto fail;

	step = "card validation";
	if (!sd_card.is_mem || sd_card.is_mmc ||
	    sd_card.log_bus_width != 2 ||
	    sd_card.csd.sector_size != SD_SECTOR_SIZE ||
	    sd_card.csd.capacity == 0) {
		err = ESP_ERR_INVALID_RESPONSE;
		goto fail;
	}

	step = "DMA buffer allocation";
	if (sd_buffer == NULL) {
		sd_buffer = heap_caps_aligned_alloc(16, SD_SECTOR_SIZE,
		    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
		if (sd_buffer == NULL) {
			err = ESP_ERR_NO_MEM;
			goto fail;
		}
	}

#if 0
	/* Test a single sector before any DLFS access. */
	step = "test read sector 18432";
	if (sd_card.csd.capacity <= 18432) {
		err = ESP_ERR_INVALID_SIZE;
		goto fail;
	}

	ESP_LOGI("sd", "Reading sector 18432 before DLFS...");
	err = sdmmc_read_sectors(&sd_card, sd_buffer, 18432, 1);
	if (err != ESP_OK)
		goto fail;

	ESP_LOGI("sd", "Read sector 18432 PASS");
#endif /* 0 */

	sd_ready = true;
	ESP_LOGI("sd", "SD ready: %" PRIu64 " sectors, 512 bytes each",
	    (uint64_t)sd_card.csd.capacity);
	return;

fail:
	ESP_LOGE("sd", "%s failed: %s", step, esp_err_to_name(err));
	if (sd_host_release()) {
		heap_caps_free(sd_buffer);
		sd_buffer = NULL;
	}
}

/*
 * Cached availability: no card-detect signal is wired on this board.
 * I/O errors mark the card unavailable until explicit reinitialization.
 */
bool
carmon_media_present(void)
{
	return (sd_ready);
}

/* Read one sector, leaving the caller's buffer unchanged on failure. */
bool
carmon_media_read_sector(uint64_t sector, uint8_t *data)
{
	esp_err_t err;

	if (!sd_ready || data == NULL ||
	    sector >= (uint64_t)sd_card.csd.capacity || sector > SIZE_MAX)
		return (false);

	err = sdmmc_read_sectors(&sd_card, sd_buffer, (size_t)sector, 1);
	if (err != ESP_OK) {
		sd_ready = false;
		ESP_LOGE("sd", "Read sector %" PRIu64 " failed: %s",
		    sector, esp_err_to_name(err));
		return (false);
	}

	memcpy(data, sd_buffer, SD_SECTOR_SIZE);
	return (true);
}

/* Write one sector. Preserve the original opt-in for writing the MBR. */
bool
carmon_media_write_sector(uint64_t sector, const void *data, bool allow_zero)
{
	esp_err_t err;

	if (!sd_ready || data == NULL ||
	    sector >= (uint64_t)sd_card.csd.capacity || sector > SIZE_MAX ||
	    (sector == 0 && !allow_zero))
		return (false);

	memcpy(sd_buffer, data, SD_SECTOR_SIZE);
	err = sdmmc_write_sectors(&sd_card, sd_buffer, (size_t)sector, 1);
	if (err != ESP_OK) {
		/* Do not retry: the failed operation may have written data. */
		sd_ready = false;
		ESP_LOGE("sd", "Write sector %" PRIu64 " failed: %s",
		    sector, esp_err_to_name(err));
		return (false);
	}

	return (true);
}

/* Capacity is expressed in sectors, as required by DLFS. */
uint64_t
carmon_media_capacity(void)
{
	if (!sd_ready)
		return (0);

	return ((uint64_t)sd_card.csd.capacity);
}
