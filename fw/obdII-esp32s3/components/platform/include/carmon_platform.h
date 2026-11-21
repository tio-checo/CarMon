#ifndef CARMON_PLATFORM_H
#define CARMON_PLATFORM_H
#include <stdbool.h>
#include "esp_err.h"
enum {
    CARMON_GPIO_HEARTBEAT = 9,
    CARMON_GPIO_LED_RED = 11, CARMON_GPIO_LED_GREEN = 10,
    CARMON_GPIO_CAN_RX = 38, CARMON_GPIO_CAN_TX = 39,
    CARMON_GPIO_CAN_SILENT = 40, CARMON_GPIO_CAN_SHUTDOWN = 41,
    CARMON_GPIO_I2C_SDA = 2, CARMON_GPIO_I2C_SCL = 1
};
esp_err_t carmon_board_init(void);
bool carmon_platform_ready(void);
int carmon_app_init(void);
void carmon_request_restart(void);
bool carmon_restart_requested(void);
int carmon_gps_uart_init(void);
int carmon_gps_uart_read(unsigned char *ch);
int carmon_gps_uart_suspend(void);
#endif
