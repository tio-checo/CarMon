#include <errno.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "carmon_platform.h"
#include "can_hal.h"
#include "master_comm.h"
#include "mon.h"
#include "obd.h"
#include "pic_core.h"
#include "rt_light.h"
#include "network.h"

/* Port task */
static void
port_task(void *arg)
{
	(void)arg;

	ESP_ERROR_CHECK(carmon_board_init());
	ESP_ERROR_CHECK(rtl_init());

	int result = carmon_app_init();

	if (result == ENOTSUP) {
		ESP_LOGW("carmon",
		    "Partial port: protocol and CAN/OBD services enabled;"
		    " remaining application services disabled");
	} else {
		ESP_ERROR_CHECK(result);
	}

	ESP_ERROR_CHECK(rtl_svc_handler_enable());
	ESP_LOGI("carmon", "Original RTL started on CPU%d: GPTimer period=%d"
	    " us; D1=GPIO9", xPortGetCoreID(), RTL_SERVICE_PERIOD);
	ESP_ERROR_CHECK(network_init());

	for (;;) {
		if (carmon_restart_requested()) {
			rtl_svc_handler_disable();
			esp_restart();
		}

		vTaskDelay(pdMS_TO_TICKS(10));

		/*
		 * Call service routines which do not need to be called
		 * from RTL
		 */
		mcom_svc();
		pcore_svc();

		if (!mon_is_diag_mode()) {
			if (can_svc() == 0) {
				obd_svc();
			}
		}
	}
}

/* Main application */
void
app_main(void)
{
	if (xTaskCreatePinnedToCore(port_task, "carmon", 8192, NULL, 5,
	    NULL, 1) != pdPASS) {
		ESP_LOGE("carmon", "Cannot create CPU1 initialization task");
		abort();
	}
}
