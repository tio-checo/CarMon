#include <stdio.h>
#include <string.h>

#include "network_test.h"
#include "wifi.h"

const char *WIFI_EVENT = "wifi", *IP_EVENT = "ip";
static esp_event_handler_t wifi_handler, ip_handler;
static esp_netif_t station;
static atomic_uint connects, disconnects;
static unsigned int stops, deinits, destroys;
static esp_err_t nvs_result;

esp_err_t nvs_flash_init(void) { return nvs_result; }
esp_err_t esp_netif_init(void) { return ESP_OK; }
esp_err_t esp_event_loop_create_default(void) { return ESP_OK; }
esp_netif_t *esp_netif_create_default_wifi_sta(void) { return &station; }
void esp_netif_destroy_default_wifi(void *p) { assert(p == &station); destroys++; }
esp_err_t esp_wifi_init(const wifi_init_config_t *p) { return ESP_OK; }
esp_err_t esp_wifi_deinit(void) { deinits++; return ESP_OK; }
esp_err_t esp_wifi_set_storage(int storage) { assert(storage == WIFI_STORAGE_RAM); return ESP_OK; }
esp_err_t esp_wifi_set_mode(int mode) { assert(mode == WIFI_MODE_STA); return ESP_OK; }
esp_err_t esp_wifi_set_config(int mode, wifi_config_t *config)
{
	assert(mode == WIFI_IF_STA);
	assert(strcmp((char *)config->sta.ssid, "test-router") == 0);
	assert(strcmp((char *)config->sta.password, "host-test-password") == 0);
	assert(config->sta.threshold.authmode == WIFI_AUTH_WPA2_PSK);
	return ESP_OK;
}

esp_err_t
esp_event_handler_instance_register(esp_event_base_t base, int32_t id,
    esp_event_handler_t handler, void *arg, esp_event_handler_instance_t *instance)
{
	assert(id == ESP_EVENT_ANY_ID && arg == NULL);
	if (base == WIFI_EVENT)
		wifi_handler = handler;
	else
		ip_handler = handler;
	*instance = (void *)base;
	return ESP_OK;
}

esp_err_t
esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id,
    esp_event_handler_instance_t instance)
{
	assert(instance == (void *)base && id == ESP_EVENT_ANY_ID);
	if (base == WIFI_EVENT)
		wifi_handler = NULL;
	else
		ip_handler = NULL;
	return ESP_OK;
}

esp_err_t esp_wifi_start(void)
{
	wifi_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_START, NULL);
	return ESP_OK;
}
esp_err_t esp_wifi_stop(void) { stops++; return ESP_OK; }
esp_err_t esp_wifi_connect(void) { atomic_fetch_add(&connects, 1); return ESP_OK; }
esp_err_t esp_wifi_disconnect(void)
{
	wifi_event_sta_disconnected_t event = { .reason = 8 };

	atomic_fetch_add(&disconnects, 1);
	wifi_handler(NULL, WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &event);
	return ESP_OK;
}
esp_err_t esp_netif_get_ip_info(esp_netif_t *p, esp_netif_ip_info_t *ip)
{
	assert(p == &station);
	*ip = (esp_netif_ip_info_t){ .ip = { 192, 168, 0, 132 },
	    .gw = { 192, 168, 0, 1 } };
	return ESP_OK;
}
esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap)
{
	ap->rssi = -69;
	ap->primary = 11;
	return ESP_OK;
}

static void
wifi_test_wait(atomic_uint *counter, unsigned int target)
{
	for (int i = 0; i < 400 && atomic_load(counter) < target; i++)
		vTaskDelay(10);
	assert(atomic_load(counter) >= target);
}

int
main(void)
{
	uint32_t generation;
	unsigned int count;

	assert(!wifi_is_connected());
	assert(wifi_init(NULL, "password") == ESP_ERR_INVALID_ARG);
	assert(wifi_init("", "password") == ESP_ERR_INVALID_ARG);
	assert(wifi_init("test-router", "short") == ESP_ERR_INVALID_ARG);
	nvs_result = ESP_FAIL;
	assert(wifi_init("test-router", "host-test-password") == ESP_FAIL);
	assert(wifi_handler == NULL);
	nvs_result = ESP_OK;
	atomic_store(&network_test_fail_task, true);
	assert(wifi_init("test-router", "host-test-password") == ESP_ERR_NO_MEM);
	assert(stops == 1 && deinits == 1 && destroys == 1);
	assert(wifi_handler == NULL && ip_handler == NULL);
	assert(wifi_init("test-router", "host-test-password") == ESP_OK);
	assert(wifi_init("test-router", "host-test-password") == ESP_ERR_INVALID_STATE);
	wifi_test_wait(&connects, 1);
	assert(!wifi_is_connected());
	ip_handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
	assert(wifi_is_connected());
	generation = wifi_get_generation();

	/* Even a disconnect/reconnect within one task timeslice invalidates TCP. */
	esp_wifi_disconnect();
	assert(!wifi_is_connected() && wifi_get_generation() != generation);
	ip_handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
	assert(wifi_is_connected() && wifi_get_generation() != generation);
	vTaskDelay(100);

	/* A real disconnect schedules another connection attempt. */
	count = atomic_load(&connects);
	esp_wifi_disconnect();
	wifi_test_wait(&connects, count + 1);

	/* Move the test clock to verify the DHCP timeout without a 30-s wait. */
	vTaskDelay(50);
	count = atomic_load(&disconnects);
	network_test_advance_time(31000000LL);
	wifi_test_wait(&disconnects, count + 1);
	wifi_test_wait(&connects, atomic_load(&connects) + 1);
	ip_handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
	generation = wifi_get_generation();
	ip_handler(NULL, IP_EVENT, IP_EVENT_STA_LOST_IP, NULL);
	assert(!wifi_is_connected() && wifi_get_generation() != generation);
	wifi_test_wait(&connects, atomic_load(&connects) + 1);
	ip_handler(NULL, IP_EVENT, IP_EVENT_STA_GOT_IP, NULL);
	assert(wifi_is_connected());
	puts("WIFI PASS: validation, RAM credentials, init cleanup, reconnect, DHCP timeout and link generations");
	return 0;
}
