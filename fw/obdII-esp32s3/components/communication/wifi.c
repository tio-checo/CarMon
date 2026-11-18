/*********************************************************************
 *
 * Wi-Fi station connection and recovery after loss of the access point.
 * All connection attempts and diagnostic output run in a task, not RTL.
 *
 *********************************************************************/

#include <stdatomic.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "wifi.h"

/* constants */
#define WIFI_EV_START		BIT0
#define WIFI_EV_DISCONNECTED	BIT1
#define WIFI_EV_GOT_IP		BIT2
#define WIFI_EV_LOST_IP		BIT3
#define WIFI_EV_ALL		(WIFI_EV_START | WIFI_EV_DISCONNECTED | \
				    WIFI_EV_GOT_IP | WIFI_EV_LOST_IP)
#define WIFI_RETRY_US		2000000LL
#define WIFI_CONNECT_TIMEOUT_US	30000000LL

/* local variables */
static const char *wifi_tag = "carmon_wifi";
static EventGroupHandle_t wifi_events;
static esp_netif_t *wifi_station;
static atomic_bool wifi_connected;
static atomic_uint_least32_t wifi_generation;
static atomic_uint wifi_disconnect_reason;

/* local functions */

/*
 * Publish state only. Formatting messages on sys_evt's small stack caused
 * a stack overflow in an earlier hardware test, so leave it to wifi_task.
 */
static void
wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
	(void)arg;

	if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
		xEventGroupSetBits(wifi_events, WIFI_EV_START);
	} else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
		const wifi_event_sta_disconnected_t *event = data;

		atomic_store(&wifi_connected, false);
		atomic_fetch_add(&wifi_generation, 1);
		atomic_store(&wifi_disconnect_reason, event->reason);
		xEventGroupSetBits(wifi_events, WIFI_EV_DISCONNECTED);
	} else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
		atomic_fetch_add(&wifi_generation, 1);
		atomic_store(&wifi_connected, true);
		xEventGroupSetBits(wifi_events, WIFI_EV_GOT_IP);
	} else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
		atomic_store(&wifi_connected, false);
		atomic_fetch_add(&wifi_generation, 1);
		xEventGroupSetBits(wifi_events, WIFI_EV_LOST_IP);
	}
}

/*
 * Keep retrying after a disconnect. Limit each attempt, including DHCP,
 * so an associated station without an address cannot remain stuck forever.
 */
static void
wifi_task(void *arg)
{
	int64_t next_connect = 0;
	int64_t connect_deadline = 0;

	(void)arg;

	for (;;) {
		EventBits_t events;
		int64_t now;
		esp_err_t err;

		events = xEventGroupWaitBits(wifi_events, WIFI_EV_ALL, pdTRUE,
		    pdFALSE, pdMS_TO_TICKS(1000));
		now = esp_timer_get_time();

		if (events & WIFI_EV_DISCONNECTED) {
			ESP_LOGW(wifi_tag, "Disconnected, reason=%u; retrying",
			    (unsigned int)atomic_load(&wifi_disconnect_reason));
			connect_deadline = 0;
			next_connect = now + WIFI_RETRY_US;
		}

		if ((events & WIFI_EV_LOST_IP) && !wifi_is_connected()) {
			ESP_LOGW(wifi_tag, "IPv4 address lost; reconnecting");
			esp_wifi_disconnect();
			connect_deadline = 0;
			next_connect = now + WIFI_RETRY_US;
		}

		if ((events & WIFI_EV_GOT_IP) && wifi_is_connected()) {
			esp_netif_ip_info_t ip;
			wifi_ap_record_t ap;

			connect_deadline = 0;
			if (esp_netif_get_ip_info(wifi_station, &ip) == ESP_OK) {
				ESP_LOGI(wifi_tag, "Connected: IP=" IPSTR
				    ", gateway=" IPSTR, IP2STR(&ip.ip),
				    IP2STR(&ip.gw));
			}
			if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
				ESP_LOGI(wifi_tag, "RSSI=%d dBm, channel=%u",
				    ap.rssi, (unsigned int)ap.primary);
			}
		}

		if (wifi_is_connected())
			continue;

		if (connect_deadline != 0 && now >= connect_deadline) {
			ESP_LOGW(wifi_tag, "Connection/DHCP timeout; retrying");
			esp_wifi_disconnect();
			connect_deadline = 0;
			next_connect = now + WIFI_RETRY_US;
		}

		if (connect_deadline != 0 || now < next_connect)
			continue;

		err = esp_wifi_connect();
		if (err == ESP_OK) {
			connect_deadline = now + WIFI_CONNECT_TIMEOUT_US;
		} else {
			ESP_LOGW(wifi_tag, "Connect failed: %s",
			    esp_err_to_name(err));
			next_connect = now + WIFI_RETRY_US;
		}
	}
}

/* public functions */

/*
 * Initialize the station, then let wifi_task maintain the connection.
 * An existing NVS partition is never erased on an initialization error.
 */
esp_err_t
wifi_init(const char *ssid, const char *password)
{
	wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
	wifi_config_t config = { 0 };
	esp_event_handler_instance_t wifi_handler = NULL;
	esp_event_handler_instance_t ip_handler = NULL;
	bool driver_initialized = false;
	bool driver_started = false;
	esp_err_t err;
	size_t ssid_len, password_len;

	if (ssid == NULL || password == NULL)
		return ESP_ERR_INVALID_ARG;
	ssid_len = strlen(ssid);
	password_len = strlen(password);
	if (ssid_len == 0 || ssid_len > sizeof (config.sta.ssid) ||
	    password_len < 8 || password_len > 63)
		return ESP_ERR_INVALID_ARG;
	if (wifi_events != NULL)
		return ESP_ERR_INVALID_STATE;

	err = nvs_flash_init();
	if (err != ESP_OK)
		return err;
	err = esp_netif_init();
	if (err != ESP_OK)
		return err;
	err = esp_event_loop_create_default();
	if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
		return err;

	wifi_events = xEventGroupCreate();
	if (wifi_events == NULL)
		return ESP_ERR_NO_MEM;
	wifi_station = esp_netif_create_default_wifi_sta();
	if (wifi_station == NULL) {
		err = ESP_ERR_NO_MEM;
		goto fail;
	}
	err = esp_wifi_init(&init);
	if (err != ESP_OK)
		goto fail;
	driver_initialized = true;

	err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
	    wifi_event, NULL, &wifi_handler);
	if (err != ESP_OK)
		goto fail;
	err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID,
	    wifi_event, NULL, &ip_handler);
	if (err != ESP_OK)
		goto fail;

	/* Do not save the password to flash when setting station configuration. */
	err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
	if (err != ESP_OK)
		goto fail;
	err = esp_wifi_set_mode(WIFI_MODE_STA);
	if (err != ESP_OK)
		goto fail;
	memcpy(config.sta.ssid, ssid, ssid_len);
	memcpy(config.sta.password, password, password_len);
	config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	err = esp_wifi_set_config(WIFI_IF_STA, &config);
	memset(&config, 0, sizeof (config));
	if (err != ESP_OK)
		goto fail;
	err = esp_wifi_start();
	if (err != ESP_OK)
		goto fail;
	driver_started = true;
	if (xTaskCreatePinnedToCore(wifi_task, "wifi", 6144, NULL, 5,
	    NULL, 0) != pdPASS) {
		err = ESP_ERR_NO_MEM;
		goto fail;
	}

	return ESP_OK;

fail:
	if (driver_started)
		esp_wifi_stop();
	if (ip_handler != NULL)
		esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID,
		    ip_handler);
	if (wifi_handler != NULL)
		esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID,
		    wifi_handler);
	if (driver_initialized)
		esp_wifi_deinit();
	if (wifi_station != NULL)
		esp_netif_destroy_default_wifi(wifi_station);
	atomic_store(&wifi_connected, false);
	atomic_fetch_add(&wifi_generation, 1);
	vEventGroupDelete(wifi_events);
	wifi_station = NULL;
	wifi_events = NULL;
	return err;
}

bool
wifi_is_connected(void)
{
	return atomic_load(&wifi_connected);
}

uint32_t
wifi_get_generation(void)
{
	return atomic_load(&wifi_generation);
}
