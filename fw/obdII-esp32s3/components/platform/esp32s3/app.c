#include <errno.h>
#include "carmon_platform.h"
#include "carmon_media.h"
#include "bt.h"
#include "can_hal.h"
#include "carmon.h"
#include "dlfs.h"
#include "ee24.h"
#include "esp_log.h"
#include "gps.h"
#include "i2c_hal.h"
#include "lcd_term.h"
#include "master_comm.h"
#include "mon.h"
#include "obd.h"
#include "pcf8563.h"
#include "pic_core.h"
#include "usb_app.h"

#define INIT(call) do { \
	int err; \
	err = (call); \
	if (err != 0) { \
		return (err); \
	} \
} while (0)

int
carmon_app_init(void)
{
	int err;

	/*
	 * Keep the application linked, but do not run it against placeholder
	 * drivers. In particular cm_init() would try to create a log
	 * on an existing card.
	 */
	INIT(i2c_init());
	INIT(ee24_init());
	INIT(pcf8563_init());
	INIT(mon_init());

	/*
	 * Initialize storage without preventing diagnostics from starting
	 * when the card is absent or cannot be read.
	 */
	err = dlfs_init();
	if (err != 0) {
		ESP_LOGW("carmon", "DLFS initialization failed: %d", err);
	} else {
		ESP_LOGI("carmon",
		    "DLFS media attached; legacy format not verified");
	}

	/*
	 * Initialize the ported services while the RTL timer
	 * is still stopped.
	 */
	INIT(mcom_init());
	INIT(pcore_init());
	INIT(can_init());
	INIT(obd_init());

	if (!carmon_platform_ready()) {
		return (ENOTSUP);
	}
/*
	Bluetooth is unused on ESP32-S3
	INIT(bt_init());
*/
	INIT(lcdterm_init());
/*
	INIT(can_init());
	INIT(obd_init());
	INIT(mcom_init());
	INIT(pcore_init());
*/
	INIT(uapp_init());
	INIT(cm_init());
	INIT(gps_init());

	return (0);
}
#undef INIT
