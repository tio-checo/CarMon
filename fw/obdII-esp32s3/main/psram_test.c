#include "psram_test.h"

#include <inttypes.h>
#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define EXPECTED_PSRAM_BYTES (8U * 1024U * 1024U)
#define TEST_ALIGNMENT_BYTES (64U * 1024U)
#define MIN_TEST_BYTES (1024U * 1024U)
#define YIELD_WORDS (64U * 1024U)
#define PATTERN_COUNT 6U

static uint32_t test_pattern(unsigned pass, size_t index)
{
    static const uint32_t fixed[] = {0x00000000U, 0xffffffffU,
                                     0xaaaaaaaaU, 0x55555555U};
    if (pass < 4U) {
        return fixed[pass];
    }
    /* Different words at different addresses also expose address aliasing. */
    const uint32_t address_pattern = (uint32_t)index * 0x9e3779b9U ^ 0xa5a5a5a5U;
    return pass == 4U ? address_pattern : ~address_pattern;
}

bool psram_run_test(void)
{
    if (!esp_psram_is_initialized()) {
        printf("PSRAM FAIL: memory was not initialized; check boot log.\n");
        return false;
    }

    const size_t detected = esp_psram_get_size();
    printf("PSRAM: detected %zu bytes; expected %u bytes (Octal, 40 MHz).\n",
           detected, EXPECTED_PSRAM_BYTES);
    if (detected != EXPECTED_PSRAM_BYTES) {
        printf("PSRAM FAIL: unexpected capacity.\n");
        return false;
    }

    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const size_t bytes = heap_caps_get_largest_free_block(caps)
                         / TEST_ALIGNMENT_BYTES * TEST_ALIGNMENT_BYTES;
    if (bytes < MIN_TEST_BYTES) {
        printf("PSRAM FAIL: largest test block is only %zu bytes.\n", bytes);
        return false;
    }
    uint32_t *allocation = heap_caps_malloc(bytes, caps);
    if (allocation == NULL) {
        printf("PSRAM FAIL: cannot allocate %zu bytes.\n", bytes);
        return false;
    }

    volatile uint32_t *memory = allocation;
    const size_t words = bytes / sizeof(*allocation);
    const int64_t started = esp_timer_get_time();
    printf("PSRAM: testing %zu bytes in a private allocation at %p.\n",
           bytes, (void *)allocation);
    for (unsigned pass = 0; pass < PATTERN_COUNT; ++pass) {
        printf("PSRAM: pattern %u/%u, write then verify...\n",
               pass + 1U, PATTERN_COUNT);
        /* Write the entire block before reading it back: it exceeds the cache. */
        for (size_t i = 0; i < words; ++i) {
            memory[i] = test_pattern(pass, i);
            if ((i + 1U) % YIELD_WORDS == 0U) {
                vTaskDelay(1);
            }
        }
        for (size_t i = 0; i < words; ++i) {
            const uint32_t expected = test_pattern(pass, i);
            const uint32_t actual = memory[i];
            if (actual != expected) {
                printf("PSRAM FAIL: pattern %u, offset 0x%zx, "
                       "expected 0x%08" PRIx32 ", read 0x%08" PRIx32 "\n",
                       pass + 1U, i * sizeof(*allocation), expected, actual);
                heap_caps_free(allocation);
                return false;
            }
            if ((i + 1U) % YIELD_WORDS == 0U) {
                vTaskDelay(1);
            }
        }
    }

    const int64_t elapsed_ms = (esp_timer_get_time() - started) / 1000;
    const bool heap_ok = heap_caps_check_integrity_addr((intptr_t)allocation, true);
    heap_caps_free(allocation);
    if (!heap_ok) {
        printf("PSRAM FAIL: heap integrity check failed.\n");
        return false;
    }
    printf("PSRAM PASS: %zu bytes, %u patterns, %" PRId64 " ms.\n",
           bytes, PATTERN_COUNT, elapsed_ms);
    return true;
}
