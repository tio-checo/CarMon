#include "internet_test.h"

#include <stdio.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#define TEST_HOST "dl.espressif.com"
#define TEST_URL "https://" TEST_HOST "/dl/esp-idf/ci/esp_http_client_demo.txt"

static esp_err_t count_response(esp_http_client_event_t *event)
{
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        size_t *received = event->user_data;
        *received += (size_t)event->data_len;
    }
    return ESP_OK;
}

bool internet_run_test(void)
{
    printf("Internet: resolving %s...\n", TEST_HOST);
    const struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_STREAM};
    struct addrinfo *address = NULL;
    const int dns_result = getaddrinfo(TEST_HOST, "443", &hints, &address);
    if (dns_result != 0 || address == NULL) {
        printf("INTERNET FAIL: DNS lookup failed (code=%d).\n", dns_result);
        if (address != NULL) freeaddrinfo(address);
        return false;
    }
    char ip[INET_ADDRSTRLEN];
    const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)address->ai_addr;
    const char *formatted_ip = inet_ntop(AF_INET, &ipv4->sin_addr, ip, sizeof(ip));
    printf("Internet DNS PASS: %s -> %s\n", TEST_HOST,
           formatted_ip != NULL ? formatted_ip : "IPv4 resolved");
    freeaddrinfo(address);

    /* Certificate expiry checks need a valid clock after a cold start. */
    printf("Internet: synchronizing time via pool.ntp.org (up to 15 seconds)...\n");
    const esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t result = esp_netif_sntp_init(&sntp);
    if (result != ESP_OK) {
        printf("INTERNET FAIL: SNTP initialization: %s\n", esp_err_to_name(result));
        return false;
    }
    result = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000));
    esp_netif_sntp_deinit();
    if (result != ESP_OK) {
        printf("INTERNET TEST INCOMPLETE: time sync failed (%s); "
               "HTTPS certificate dates cannot be checked yet. "
               "NTP uses UDP port 123.\n", esp_err_to_name(result));
        return false;
    }
    const time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S UTC", &utc);
    printf("Internet TIME PASS: %s\n", timestamp);

    size_t received = 0;
    const esp_http_client_config_t config = {
        .url = TEST_URL,
        .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .disable_auto_redirect = true,
        .event_handler = count_response,
        .user_data = &received,
    };
    printf("Internet: HTTPS GET %s (certificate verification enabled)...\n", TEST_URL);
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        printf("INTERNET FAIL: cannot allocate HTTPS client.\n");
        return false;
    }
    result = esp_http_client_perform(client);
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (result != ESP_OK || status != 200 || received == 0) {
        printf("INTERNET FAIL: HTTPS result=%s, HTTP=%d, received=%zu bytes.\n",
               esp_err_to_name(result), status, received);
        return false;
    }
    printf("INTERNET PASS: DNS + verified HTTPS, HTTP=%d, received=%zu bytes.\n",
           status, received);
    return true;
}
