#include <errno.h>
#include <stdio.h>
#include "test_idf.h"
#include "rt_light.h"
#include "carmon_platform.h"

static int core, gpio_level, call_order[32], calls, starts, stops;
static bool running, diag, enter_diag, leave_diag;
static int64_t now_us;
static alarm_cb_t alarm_callback;
int xPortGetCoreID(void) { return core; }
int gpio_set_level(int pin, int level) { assert(pin == CARMON_GPIO_HEARTBEAT); gpio_level = level; return 0; }
int64_t esp_timer_get_time(void) { return now_us; }
BOOL mon_is_diag_mode(void) { return diag ? TRUE : FALSE; }
esp_err_t gptimer_new_timer(const gptimer_config_t *c, gptimer_handle_t *h)
{ assert(core == 1 && c->resolution_hz == 1000000 && c->intr_priority == 1); *h = (void *)1; return 0; }
esp_err_t gptimer_set_alarm_action(gptimer_handle_t h, const gptimer_alarm_config_t *a)
{ assert(a->alarm_count == 500 && a->reload_count == 0 && a->flags.auto_reload_on_alarm); return 0; }
esp_err_t gptimer_register_event_callbacks(gptimer_handle_t h, const gptimer_event_callbacks_t *c, void *arg)
{ alarm_callback = c->on_alarm; return 0; }
esp_err_t gptimer_enable(gptimer_handle_t h) { return 0; }
esp_err_t gptimer_start(gptimer_handle_t h) { assert(!running); running = true; ++starts; return 0; }
esp_err_t gptimer_stop(gptimer_handle_t h) { assert(running); running = false; ++stops; return 0; }
static void tick(void)
{ assert(running); now_us += 500; gptimer_alarm_event_data_t e = {0}; assert(!alarm_callback((void *)1, &e, NULL)); }
static int first(void) { call_order[calls++] = 1; if (enter_diag) diag = true; return 0; }
static int second(void) { call_order[calls++] = 2; if (leave_diag) diag = false; return 0; }
static int third(void) { call_order[calls++] = 3; return 0; }

int main(void)
{
    assert(rtl_init() == EINVAL); /* Must allocate the interrupt on CPU1. */
    core = 1;
    assert(rtl_init() == 0);
    assert(rtl_init() == EINVAL);
    rtl_timer_t *timers[17] = {0};
    for (int i = 0; i < 16; ++i) assert(rtl_timer_init(&timers[i], 1000) == 0);
    assert(rtl_timer_init(&timers[16], 1000) == ENOSPC);
    assert(timers[16] == NULL); /* Exhaustion must not return an out-of-bounds slot. */
    assert(rtl_timer_init(&timers[0], 500) == 0); /* Reuse when pool is full. */
    int a = rtl_svc_init("first", first, TRUE, FALSE);
    int b = rtl_svc_init("second", second, TRUE, TRUE);
    assert(a == 0 && b == 1);
    assert(rtl_svc_enable(-1) == EINVAL && rtl_svc_disable(16) == EINVAL);
    assert(rtl_svc_handler_enable() == 0 && rtl_svc_handler_enable() == 0 && starts == 1);
    tick();
    assert(rtl_timer_expired(timers[0]) && !rtl_timer_expired(timers[1]));
    assert(calls == 2 && call_order[0] == 1 && call_order[1] == 2);
    tick();
    assert(rtl_timer_expired(timers[1]));
    diag = true; calls = 0; tick();
    assert(calls == 1 && call_order[0] == 2);
    assert(rtl_svc_disable(b) == 0); calls = 0; tick(); assert(calls == 0);
    assert(rtl_svc_enable(b) == 0); tick(); assert(calls == 1);
    /* Once entered, diagnostic filtering lasts until the end of this pass,
     * even if another service leaves diagnostic mode in the same interrupt. */
    assert(rtl_svc_init("third", third, TRUE, FALSE) == 2);
    diag = false; enter_diag = true; leave_diag = true; calls = 0;
    tick();
    assert(calls == 2 && call_order[0] == 1 && call_order[1] == 2);
    enter_diag = false; leave_diag = false; calls = 0;
    tick();
    assert(calls == 3 && call_order[2] == 3);
    rtl_hb_mode(HB_MODE_ON, 0, 0); tick(); assert(gpio_level == 0);
    rtl_hb_mode(HB_MODE_OFF, 0, 0); tick(); assert(gpio_level == 1);
    assert(rtl_suspend() == 0 && !running && stops == 1);
    assert(rtl_svc_handler_disable() == 0 && stops == 1);
    puts("RTL PASS: CPU affinity, 500-us alarm, pool exhaustion, timer expiry, service order/filtering and heartbeat");
    return 0;
}
