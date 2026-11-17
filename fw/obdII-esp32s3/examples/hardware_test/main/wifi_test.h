#pragma once

/* Start an independent task; credentials are entered over the USB console. */
void wifi_test_start(void);
const char *wifi_test_status(void);

/* Print connected-AP RSSI; call once per second from an application task. */
void wifi_test_print_rssi(void);
