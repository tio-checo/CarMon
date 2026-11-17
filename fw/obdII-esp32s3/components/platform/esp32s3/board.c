#include <stdatomic.h>
#include "driver/gpio.h"
#include "carmon_platform.h"

static atomic_bool restart_pending;

esp_err_t carmon_board_init(void)
{
    /* Active-low LED off; keep the TCAN330 silent and in shutdown. */
    esp_err_t err = gpio_set_level(CARMON_GPIO_HEARTBEAT, 1);
    if (err != ESP_OK) return err;
    err = gpio_set_level(CARMON_GPIO_CAN_SILENT, 1);
    if (err != ESP_OK) return err;
    err = gpio_set_level(CARMON_GPIO_CAN_SHUTDOWN, 1);
    if (err != ESP_OK) return err;
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << CARMON_GPIO_HEARTBEAT) |
                        (1ULL << CARMON_GPIO_CAN_SILENT) |
                        (1ULL << CARMON_GPIO_CAN_SHUTDOWN),
        .mode = GPIO_MODE_OUTPUT,
    };
    return gpio_config(&config);
}

void carmon_request_restart(void) { atomic_store(&restart_pending, true); }
bool carmon_restart_requested(void) { return atomic_load(&restart_pending); }
