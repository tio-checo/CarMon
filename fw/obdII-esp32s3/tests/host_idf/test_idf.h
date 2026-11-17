#ifndef TEST_IDF_H
#define TEST_IDF_H
/* Host doubles for IDF entry points; these do not emulate interrupt latency. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERROR_CHECK(expr) assert((expr) == ESP_OK)
#define GPTIMER_CLK_SRC_DEFAULT 0
#define GPTIMER_COUNT_UP 0
typedef void *gptimer_handle_t;
typedef struct { int clk_src; int direction; uint32_t resolution_hz; int intr_priority; } gptimer_config_t;
typedef struct { uint64_t alarm_count, reload_count; struct { bool auto_reload_on_alarm; } flags; } gptimer_alarm_config_t;
typedef struct { uint64_t count_value; } gptimer_alarm_event_data_t;
typedef bool (*alarm_cb_t)(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *);
typedef struct { alarm_cb_t on_alarm; } gptimer_event_callbacks_t;
int xPortGetCoreID(void);
int gpio_set_level(int pin, int level);
int64_t esp_timer_get_time(void);
esp_err_t gptimer_new_timer(const gptimer_config_t *, gptimer_handle_t *);
esp_err_t gptimer_set_alarm_action(gptimer_handle_t, const gptimer_alarm_config_t *);
esp_err_t gptimer_register_event_callbacks(gptimer_handle_t, const gptimer_event_callbacks_t *, void *);
esp_err_t gptimer_enable(gptimer_handle_t);
esp_err_t gptimer_start(gptimer_handle_t);
esp_err_t gptimer_stop(gptimer_handle_t);
#endif
