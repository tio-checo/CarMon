#ifndef NETWORK_TEST_H
#define NETWORK_TEST_H

/* Task/event doubles for host tests. Wi-Fi radio behavior is not simulated. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
const char *esp_err_to_name(esp_err_t);

#define BIT0 1U
#define BIT1 2U
#define BIT2 4U
#define BIT3 8U
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdMS_TO_TICKS(ms) (ms)
typedef uint32_t TickType_t;
typedef uint32_t EventBits_t;
typedef struct network_test_events *EventGroupHandle_t;
EventGroupHandle_t xEventGroupCreate(void);
EventBits_t xEventGroupSetBits(EventGroupHandle_t, EventBits_t);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t, EventBits_t, int, int,
    TickType_t);
void vEventGroupDelete(EventGroupHandle_t);
int xTaskCreatePinnedToCore(void (*)(void *), const char *, uint32_t, void *,
    unsigned int, void *, int);
void vTaskDelay(TickType_t);
void vTaskDelete(void *);
int64_t esp_timer_get_time(void);
void network_test_advance_time(int64_t);
extern atomic_bool network_test_fail_task;
void network_test_log(const char *, const char *, ...);
#define ESP_LOGI network_test_log
#define ESP_LOGW network_test_log
#define ESP_LOGE network_test_log

typedef const char *esp_event_base_t;
typedef void *esp_event_handler_instance_t;
typedef void (*esp_event_handler_t)(void *, esp_event_base_t, int32_t, void *);
extern const char *WIFI_EVENT, *IP_EVENT;
#define ESP_EVENT_ANY_ID -1
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define IP_EVENT_STA_GOT_IP 3
#define IP_EVENT_STA_LOST_IP 4
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_instance_register(esp_event_base_t, int32_t,
    esp_event_handler_t, void *, esp_event_handler_instance_t *);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t, int32_t,
    esp_event_handler_instance_t);

typedef struct { unsigned int reason; } wifi_event_sta_disconnected_t;
typedef struct { int unused; } esp_netif_t;
typedef struct { uint8_t a, b, c, d; } esp_ip4_addr_t;
typedef struct { esp_ip4_addr_t ip, netmask, gw; } esp_netif_ip_info_t;
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(ip) (ip)->a, (ip)->b, (ip)->c, (ip)->d
esp_err_t esp_netif_init(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
void esp_netif_destroy_default_wifi(void *);
esp_err_t esp_netif_get_ip_info(esp_netif_t *, esp_netif_ip_info_t *);

typedef struct { int unused; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() { 0 }
typedef struct {
    struct {
        uint8_t ssid[32], password[64];
        struct { int authmode; } threshold;
    } sta;
} wifi_config_t;
typedef struct { int rssi; unsigned int primary; } wifi_ap_record_t;
#define WIFI_STORAGE_RAM 1
#define WIFI_MODE_STA 1
#define WIFI_IF_STA 1
#define WIFI_AUTH_WPA2_PSK 3
esp_err_t nvs_flash_init(void);
esp_err_t esp_wifi_init(const wifi_init_config_t *);
esp_err_t esp_wifi_deinit(void);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_set_config(int, wifi_config_t *);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *);

#endif
