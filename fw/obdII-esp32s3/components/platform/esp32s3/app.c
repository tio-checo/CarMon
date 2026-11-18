#include <errno.h>
#include "carmon_platform.h"
#include "bt.h"
#include "can_hal.h"
#include "carmon.h"
#include "dlfs.h"
#include "ee24.h"
#include "gps.h"
#include "i2c_hal.h"
#include "lcd_term.h"
#include "master_comm.h"
#include "mon.h"
#include "obd.h"
#include "pcf8563.h"
#include "pic_core.h"
#include "usb_app.h"

int carmon_app_init(void)
{
    /* Keep the application linked, but do not run it against placeholder drivers.
     * In particular cm_init() would try to create a log on an existing card. */
    if (!carmon_platform_ready()) {
        /* Start only the protocol services, with the RTL timer still stopped. */
        mcom_init();
        pcore_init();
        return ENOTSUP;
    }
    int err;
#define INIT(call) do { err = (call); if (err != 0) return err; } while (0)
    INIT(bt_init());
    INIT(i2c_init());
    INIT(pcf8563_init());
    INIT(lcdterm_init());
    INIT(can_init());
    INIT(obd_init());
    INIT(mcom_init());
    INIT(mon_init());
    INIT(pcore_init());
    INIT(uapp_init());
    INIT(dlfs_init());
    INIT(cm_init());
    INIT(gps_init());
    INIT(ee24_init());
#undef INIT
    return 0;
}
