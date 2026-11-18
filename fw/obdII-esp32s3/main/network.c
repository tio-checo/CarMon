/*********************************************************************
 *
 * Start the network transport using credentials selected in menuconfig.
 * Connection management remains separate in the Wi-Fi module.
 *
 *********************************************************************/

#include <stdbool.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "network.h"
#include "tcp.h"
#include "wifi.h"

/* local variables */
static const char *network_tag = "network";
static bool network_started;

/* local functions */

/*
 * Start without waiting for a USB host. Configuration strings are compiled
 * into the firmware; the Wi-Fi driver does not save another copy in NVS.
 */
static void
network_task(void *arg)
{
	esp_err_t err;

	(void)arg;

	err = wifi_init(CONFIG_CARMON_WIFI_SSID, CONFIG_CARMON_WIFI_PASSWORD);
	if (err == ESP_ERR_INVALID_ARG) {
		ESP_LOGE(network_tag, "Set WiFi SSID (1-32 bytes) and password "
		    "(8-63 bytes) in idf.py menuconfig -> CarMon network");
	}
	if (err != ESP_OK)
		goto fail;
	err = tcp_server_init(CONFIG_CARMON_TCP_PORT);
	if (err != ESP_OK)
		goto fail;
	ESP_LOGI(network_tag, "Waiting for WiFi/DHCP; TCP port=%u",
	    (unsigned int)CONFIG_CARMON_TCP_PORT);
	ESP_LOGI(network_tag, "CarMon protocol over TCP; peripheral backends are not active");
	vTaskDelete(NULL);
	return;

fail:
	ESP_LOGE(network_tag, "Network initialization failed: %s",
	    esp_err_to_name(err));
	vTaskDelete(NULL);
}

/* public functions */
esp_err_t
network_init(void)
{
	if (network_started)
		return ESP_ERR_INVALID_STATE;
	if (xTaskCreatePinnedToCore(network_task, "network", 4096, NULL, 5,
	    NULL, 0) != pdPASS)
		return ESP_ERR_NO_MEM;
	network_started = true;
	return ESP_OK;
}
