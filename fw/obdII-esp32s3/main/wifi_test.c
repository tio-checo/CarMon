#include "wifi_test.h"

#include <fcntl.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "internet_test.h"

#define TEST_WIFI_SSID "UPC5254179"
#define GOT_IP_BIT BIT0
#define DISCONNECTED_BIT BIT1
#define WAIT_PASSWORD_BIT BIT2
#define FAILED_BIT BIT3
#define SCAN_LIMIT 10

static EventGroupHandle_t events;
static atomic_uint disconnect_reason;

static bool wifi_ok(esp_err_t result, const char *operation)
{
    if (result == ESP_OK) {
        return true;
    }
    printf("WiFi FAIL: %s: %s\n", operation, esp_err_to_name(result));
    xEventGroupSetBits(events, FAILED_BIT);
    return false;
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    /* Runs on sys_evt's small stack. Only publish state here; all printf/IP
     * formatting and driver queries belong to the separate wifi_test task. */
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        atomic_store(&disconnect_reason, event->reason);
        xEventGroupClearBits(events, GOT_IP_BIT);
        xEventGroupSetBits(events, DISCONNECTED_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupClearBits(events, DISCONNECTED_BIT | FAILED_BIT);
        xEventGroupSetBits(events, GOT_IP_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        atomic_store(&disconnect_reason, 0);
        xEventGroupClearBits(events, GOT_IP_BIT);
        xEventGroupSetBits(events, DISCONNECTED_BIT);
    }
}

/* Echo only asterisks, handle CR/LF/CRLF, and reject overlong input. */
static bool read_password(char *password, size_t capacity)
{
    static bool discard_lf;
    size_t used = 0;
    bool overflow = false;
    memset(password, 0, capacity);
    for (;;) {
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (discard_lf) {
            discard_lf = false;
            if (c == '\n') continue;
        }
        if (c == '\n' || c == '\r') {
            discard_lf = c == '\r';
            putchar('\n');
            return !overflow;
        }
        if (c == '\b' || c == 127) {
            if (used > 0) {
                password[--used] = 0;
                fputs("\b \b", stdout);
            }
        } else if ((unsigned char)c >= 32) {
            if (used < capacity - 1U) {
                password[used++] = c;
                putchar('*');
            } else {
                overflow = true;
                putchar('\a');
            }
        }
    }
}

static void wifi_task(void *arg)
{
    /* Do not erase an existing NVS partition on initialization errors. */
    if (!wifi_ok(nvs_flash_init(), "NVS init") ||
        !wifi_ok(esp_netif_init(), "netif init") ||
        !wifi_ok(esp_event_loop_create_default(), "event loop")) {
        goto done;
    }
    esp_netif_t *station = esp_netif_create_default_wifi_sta();
    if (station == NULL) {
        wifi_ok(ESP_ERR_NO_MEM, "station interface");
        goto done;
    }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if (!wifi_ok(esp_wifi_init(&init), "driver init") ||
        !wifi_ok(esp_wifi_set_storage(WIFI_STORAGE_RAM), "RAM credential storage") ||
        !wifi_ok(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_event, NULL), "WiFi events") ||
        !wifi_ok(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL), "IP events") ||
        !wifi_ok(esp_wifi_set_mode(WIFI_MODE_STA), "station mode") ||
        !wifi_ok(esp_wifi_start(), "start")) {
        goto done;
    }

    printf("WiFi: scanning 2.4 GHz networks...\n");
    if (!wifi_ok(esp_wifi_scan_start(NULL, true), "scan")) {
        goto done;
    }
    uint16_t total = 0;
    uint16_t count = SCAN_LIMIT;
    wifi_ap_record_t records[SCAN_LIMIT];
    if (!wifi_ok(esp_wifi_scan_get_ap_num(&total), "scan count") ||
        !wifi_ok(esp_wifi_scan_get_ap_records(&count, records), "scan records")) {
        goto done;
    }
    printf("WiFi SCAN PASS: found %u APs; showing %u strongest.\n", total, count);
    for (unsigned i = 0; i < count; ++i) {
        printf("WiFi AP: %-32.32s RSSI=%d dBm, channel=%u, auth=%u\n",
               (const char *)records[i].ssid, records[i].rssi,
               (unsigned)records[i].primary, (unsigned)records[i].authmode);
    }
    /* Nonblocking VFS reads need the interrupt driver and its RX ring buffer.
     * Without it, IDF 5.5.1 reports zero available bytes even for a full FIFO. */
    usb_serial_jtag_driver_config_t usb_config = {
        .tx_buffer_size = 2048,
        .rx_buffer_size = 256,
    };
    if (!wifi_ok(usb_serial_jtag_driver_install(&usb_config), "USB input driver")) {
        goto done;
    }
    usb_serial_jtag_vfs_use_driver();
    /* Keep raw CR/LF distinctions so CRLF is consumed as a single Enter. */
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_LF);
    const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags < 0 || fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) {
        wifi_ok(ESP_FAIL, "USB console input");
        goto done;
    }

    for (;;) {
        wifi_config_t config = {0};
        char password[65];
        xEventGroupClearBits(events, FAILED_BIT | DISCONNECTED_BIT);
        xEventGroupSetBits(events, WAIT_PASSWORD_BIT);
        printf("WiFi PASSWORD: enter password for %s via USB, then Enter (* hides characters).\n",
               TEST_WIFI_SSID);
        printf("Password: ");
        if (!read_password(password, sizeof(password))) {
            memset(password, 0, sizeof(password));
            printf("WiFi: password too long; try again.\n");
            continue;
        }
        printf("WiFi: password line received (content hidden).\n");
        const size_t length = strlen(password);
        if (length != 0 && (length < 8 || length > 63)) {
            memset(password, 0, sizeof(password));
            printf("WiFi: use an 8-63 byte WPA password, or empty for an open network.\n");
            continue;
        }
        memcpy(config.sta.ssid, TEST_WIFI_SSID, sizeof(TEST_WIFI_SSID) - 1U);
        memcpy(config.sta.password, password, length);
        memset(password, 0, sizeof(password));
        config.sta.threshold.authmode = length ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
        xEventGroupClearBits(events, WAIT_PASSWORD_BIT | DISCONNECTED_BIT | GOT_IP_BIT);
        const esp_err_t result = esp_wifi_set_config(WIFI_IF_STA, &config);
        memset(&config, 0, sizeof(config));
        if (!wifi_ok(result, "station configuration") ||
            !wifi_ok(esp_wifi_connect(), "connect")) {
            continue;
        }
        printf("WiFi: connecting to %s, waiting up to 30 seconds for DHCP...\n", TEST_WIFI_SSID);
        const EventBits_t bits = xEventGroupWaitBits(events, GOT_IP_BIT | DISCONNECTED_BIT,
                                                    pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
        if (bits & GOT_IP_BIT) {
            esp_netif_ip_info_t ip;
            if (!wifi_ok(esp_netif_get_ip_info(station, &ip), "read IP address")) {
                xEventGroupClearBits(events, GOT_IP_BIT);
                esp_wifi_disconnect();
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            printf("WiFi PASS: SSID=%s, IP=" IPSTR ", gateway=" IPSTR "\n",
                   TEST_WIFI_SSID, IP2STR(&ip.ip), IP2STR(&ip.gw));
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                printf("WiFi: connected, RSSI=%d dBm, channel=%u.\n", ap.rssi, (unsigned)ap.primary);
            }
            internet_run_test();
            printf("Tests finished. D1 keeps blinking; RSSI is reported every second.\n");
            xEventGroupWaitBits(events, DISCONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
        } else {
            printf("WiFi FAIL: connection/DHCP did not complete; re-enter password to retry.\n");
        }
        if (xEventGroupGetBits(events) & DISCONNECTED_BIT) {
            const unsigned reason = atomic_load(&disconnect_reason);
            if (reason == 0) {
                printf("WiFi: IP address lost.\n");
            } else {
                printf("WiFi: disconnected, reason=%u\n", reason);
            }
        }
        esp_wifi_disconnect();
        /* Let disconnect events finish before the next attempt. */
        vTaskDelay(pdMS_TO_TICKS(200));
    }

done:
    vTaskDelete(NULL);
}

void wifi_test_start(void)
{
    events = xEventGroupCreate();
    configASSERT(events != NULL);
    if (xTaskCreate(wifi_task, "wifi_test", 16384, NULL, 5, NULL) != pdPASS) {
        wifi_ok(ESP_ERR_NO_MEM, "test task");
    }
}

const char *wifi_test_status(void)
{
    if (events == NULL) {
        return "NOT_STARTED";
    }
    const EventBits_t bits = xEventGroupGetBits(events);
    if (bits & GOT_IP_BIT) return "PASS";
    if (bits & FAILED_BIT) return "FAIL";
    if (bits & WAIT_PASSWORD_BIT) return "WAIT_PASSWORD";
    if (bits & DISCONNECTED_BIT) return "DISCONNECTED";
    return "STARTING/CONNECTING";
}

/* Called once per LED blink cycle from app_main, never from sys_evt.
 * Keep reporting even while the Wi-Fi task runs the HTTPS test. */
void wifi_test_print_rssi(void)
{
    if (events == NULL || !(xEventGroupGetBits(events) & GOT_IP_BIT)) {
        return;
    }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        printf("WiFi RSSI: uptime=%" PRId64 " s, RSSI=%d dBm, channel=%u.\n",
               esp_timer_get_time() / 1000000, ap.rssi, (unsigned)ap.primary);
    }
}
