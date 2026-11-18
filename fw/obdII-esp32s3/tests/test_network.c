#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "network_test.h"
#include "network.h"

/* Run the startup task directly, without a USB host or scheduler. */
static void (*network_test_task)(void *);
static bool network_test_fail_create = true;
static bool network_test_deleted;
static bool network_test_wifi_called;
static bool network_test_tcp_called;
static bool network_test_wifi_error;
static bool network_test_tcp_error;

int
xTaskCreatePinnedToCore(void (*function)(void *), const char *name,
    uint32_t stack, void *arg, unsigned int priority, void *handle, int core)
{
	assert(core == 0 && stack > 0 && arg == NULL && handle == NULL);
	if (network_test_fail_create) {
		network_test_fail_create = false;
		return 0;
	}
	network_test_task = function;
	return pdPASS;
}

void
vTaskDelete(void *task)
{
	assert(task == NULL);
	network_test_deleted = true;
}

const char *
esp_err_to_name(esp_err_t err)
{
	return err == ESP_ERR_INVALID_ARG ? "ESP_ERR_INVALID_ARG" : "test error";
}

void
network_test_log(const char *tag, const char *format, ...)
{
	va_list args;

	printf("%s: ", tag);
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	putchar('\n');
}

esp_err_t
wifi_init(const char *ssid, const char *password)
{
	assert(strcmp(ssid, "host-test-network") == 0);
	assert(strcmp(password, "host-test-password") == 0);
	network_test_wifi_called = true;
	return network_test_wifi_error ? ESP_ERR_INVALID_ARG : ESP_OK;
}

esp_err_t
tcp_server_init(uint16_t port)
{
	assert(port == 3333 && network_test_wifi_called);
	network_test_tcp_called = true;
	return network_test_tcp_error ? ESP_ERR_NO_MEM : ESP_OK;
}

int
main(int argc, char **argv)
{
	assert(argc == 2);
	network_test_wifi_error = strcmp(argv[1], "wifi_error") == 0;
	network_test_tcp_error = strcmp(argv[1], "tcp_error") == 0;
	close(STDIN_FILENO);
	assert(network_init() == ESP_ERR_NO_MEM);
	assert(network_init() == ESP_OK);
	assert(network_init() == ESP_ERR_INVALID_STATE);
	assert(network_test_task != NULL);
	network_test_task(NULL);
	assert(network_test_deleted && network_test_wifi_called);
	assert(network_test_tcp_called == !network_test_wifi_error);
	puts("NETWORK PASS: configured credentials, automatic startup and error handling");
	return 0;
}
