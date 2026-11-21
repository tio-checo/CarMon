# The original services have circular references. Keep one aggregate component
# while replacing the hardware backends, rather than introducing circular IDF dependencies.
idf_component_register(
    SRCS "carmon.c" "obd.c" "can_snoop.c"
         "../communication/master_comm.c"
         "../communication/wifi.c" "../communication/tcp.c"
         "../devices/ee24.c" "../devices/pcf8563.c" "../devices/lcd_term.c" "../devices/gps.c"
         "../storage/dlfs.c" "../rtl/rt_light.c"
         "../platform/utils.c" "../platform/helpers.c" "../platform/pic_core.c"
         "../platform/i2c_hal.c"
         "../platform/esp32s3/board.c" "../platform/esp32s3/app.c" "../platform/esp32s3/unported.c"
    INCLUDE_DIRS "include" "../communication/include" "../devices/include"
                 "../storage/include" "../rtl/include" "../platform/include"
    PRIV_REQUIRES esp_driver_gpio esp_driver_gptimer esp_driver_i2c esp_timer freertos
                  esp_wifi esp_event esp_netif nvs_flash lwip)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    OBDCONV_BOARD OBD_MODULE CANHAL_MODULE CARMON_MODULE
    I2CHAL_MODULE PCF8563_MODULE LCDTERM_MODULE DLFS_MODULE GPS_MODULE EE24_MODULE MCOM_TCP_MODULE)
