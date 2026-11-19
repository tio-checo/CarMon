#include <inttypes.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_err.h"
#include "esp_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_test.h"
#include "sdcard_test.h"
#include "wifi_test.h"

/* D1 cathode is LED_HB; its anode connects through R11 (1k) to 3.3 V. */
#define D1_GPIO GPIO_NUM_9
#define D1_ON_LEVEL 0
#define D1_OFF_LEVEL 1
#define D1_HALF_PERIOD_MS 500

void app_main(void)
{
    /* Flush prompts and password masking characters immediately. */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Preload the inactive level before enabling the output. */
    ESP_ERROR_CHECK(gpio_set_level(D1_GPIO, D1_OFF_LEVEL));
    const gpio_config_t led_config = {
        .pin_bit_mask = 1ULL << D1_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_config));

    esp_chip_info_t chip;
    uint32_t flash_size = 0;
    esp_chip_info(&chip);
    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &flash_size));

    printf("\nOBD-II converter: ESP32-S3 Hello World\n");
    printf("Console: internal USB Serial/JTAG\n");
    printf("Chip: ESP32-S3, %u cores, revision v%u.%u\n",
           (unsigned)chip.cores, (unsigned)(chip.revision / 100),
           (unsigned)(chip.revision % 100));
    printf("Flash: %" PRIu32 " MB\n", flash_size / (1024U * 1024U));
    printf("D1: GPIO9, active LOW, 500 ms ON / 500 ms OFF\n");

    i2c_test_run();
    sdcard_run_test();
    wifi_test_start();

    while (true) {
        ESP_ERROR_CHECK(gpio_set_level(D1_GPIO, D1_ON_LEVEL));
        vTaskDelay(pdMS_TO_TICKS(D1_HALF_PERIOD_MS));
        ESP_ERROR_CHECK(gpio_set_level(D1_GPIO, D1_OFF_LEVEL));
        vTaskDelay(pdMS_TO_TICKS(D1_HALF_PERIOD_MS));
        wifi_test_print_rssi();
    }
}
