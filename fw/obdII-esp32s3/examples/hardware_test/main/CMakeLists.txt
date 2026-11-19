idf_component_register(SRCS "hello_world.c" "wifi_test.c" "internet_test.c" "sdcard_test.c" "i2c_test.c"
                       PRIV_REQUIRES esp_driver_gpio esp_system esp_timer spi_flash esp_psram heap
                                     esp_wifi esp_event esp_netif nvs_flash esp_driver_usb_serial_jtag
                                     esp_http_client mbedtls lwip sdmmc esp_driver_sdmmc esp_driver_i2c
                       INCLUDE_DIRS ".")
