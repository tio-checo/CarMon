#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/sdmmc_host.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"
#include "sdcard_test.h"

#define READ_BLOCK_SECTORS 8U
#define READ_SAMPLE_COUNT 32U

void sdcard_run_test(void)
{
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags &= ~(SDMMC_HOST_FLAG_8BIT | SDMMC_HOST_FLAG_DDR);
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    /* J1 -> U2: CLK/22, CMD/23, D0/21, D1/20, D2/25, D3/24.
     * R4..R8 provide the external 10k pullups; leave internal ones off. */
    slot.clk = GPIO_NUM_14;
    slot.cmd = GPIO_NUM_21;
    slot.d0 = GPIO_NUM_13;
    slot.d1 = GPIO_NUM_12;
    slot.d2 = GPIO_NUM_48;
    slot.d3 = GPIO_NUM_47;
    slot.width = 4;
    slot.cd = GPIO_NUM_NC;
    slot.wp = GPIO_NUM_NC;

    bool initialized = false;
    sdmmc_card_t *card = NULL;
    unsigned char *first = NULL;
    unsigned char *second = NULL;
    const char *step = "host initialization";
    esp_err_t err;
    uint64_t bytes_read = 0;
    int64_t started = 0;

    printf("\nSD: read-only test, 4-bit SDMMC, target 20 MHz.\n");
    printf("SD: CLK=14 CMD=21 D0=13 D1=12 D2=48 D3=47.\n");
    err = sdmmc_host_init();
    if (err != ESP_OK) goto cleanup;
    initialized = true;
    step = "slot initialization";
    err = sdmmc_host_init_slot(host.slot, &slot);
    if (err != ESP_OK) goto cleanup;
    step = "card allocation";
    card = calloc(1, sizeof(*card));
    if (!card) {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    step = "card initialization";
    err = sdmmc_card_init(&host, card);
    if (err != ESP_OK) goto cleanup;
    sdmmc_card_print_info(stdout, card);
    step = "4-bit memory-card verification";
    if (!card->is_mem || card->is_mmc || card->log_bus_width != 2 ||
        card->csd.sector_size != 512 ||
        card->csd.capacity < READ_BLOCK_SECTORS) {
        err = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    const size_t block_bytes = READ_BLOCK_SECTORS * card->csd.sector_size;
    step = "DMA buffer allocation";
    first = heap_caps_aligned_alloc(16, block_bytes,
                                   MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    second = heap_caps_aligned_alloc(16, block_bytes,
                                    MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!first || !second) {
        err = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    started = esp_timer_get_time();
    for (unsigned i = 0; i < READ_SAMPLE_COUNT; ++i) {
        /* Include the first and last blocks and spread samples over the card. */
        const size_t sector = (uint64_t)(card->csd.capacity - READ_BLOCK_SECTORS)
                            * i / (READ_SAMPLE_COUNT - 1);
        step = "first block read";
        err = sdmmc_read_sectors(card, first, sector, READ_BLOCK_SECTORS);
        if (err != ESP_OK) {
            printf("SD: failed at sector %zu.\n", sector);
            goto cleanup;
        }
        step = "repeated block read";
        err = sdmmc_read_sectors(card, second, sector, READ_BLOCK_SECTORS);
        if (err != ESP_OK) {
            printf("SD: failed at sector %zu.\n", sector);
            goto cleanup;
        }
        step = "read comparison";
        if (memcmp(first, second, block_bytes) != 0) {
            printf("SD: repeated reads differ at sector %zu.\n", sector);
            err = ESP_ERR_INVALID_CRC;
            goto cleanup;
        }
        bytes_read += 2 * block_bytes;
        vTaskDelay(1);
    }
    step = "final card status";
    err = sdmmc_get_status(card);

cleanup:
    if (err == ESP_OK) {
        const uint64_t capacity = (uint64_t)card->csd.capacity * card->csd.sector_size;
        printf("SD PASS: card=%.8s, capacity=%" PRIu64 " bytes (%" PRIu64
               " MiB), bus=4-bit, clock=%d kHz, read=%" PRIu64
               " bytes, %u repeated block comparisons OK, elapsed=%" PRId64 " ms.\n",
               card->cid.name, capacity, capacity / (1024 * 1024),
               card->real_freq_khz, bytes_read, READ_SAMPLE_COUNT,
               (esp_timer_get_time() - started) / 1000);
        printf("SD: no sectors written or erased; filesystem not mounted.\n");
    } else {
        printf("SD FAIL: %s: %s (0x%x). No data written.\n",
               step, esp_err_to_name(err), (unsigned)err);
    }
    free(second);
    free(first);
    free(card);
    if (initialized) {
        const esp_err_t deinit_err = sdmmc_host_deinit();
        if (deinit_err != ESP_OK) {
            printf("SD: host cleanup failed: %s.\n", esp_err_to_name(deinit_err));
        }
    }
}
