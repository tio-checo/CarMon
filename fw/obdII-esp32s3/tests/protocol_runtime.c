#include <pthread.h>
#include <string.h>
#include <time.h>
#include "network_test.h"
#include "master_comm.h"
#include "pic_core.h"
#include "rt_light.h"

static int (*services[2])(void);
static int count;
static atomic_bool stop;
static pthread_t thread;
int rtl_svc_init(const char *name, int (*fn)(), BOOL enabled, BOOL diag)
{
    assert(count < 2 && enabled);
    services[count] = fn;
    return count++;
}
static void *tick(void *arg)
{
    while (!atomic_load(&stop)) {
        for (int i = 0; i < count; i++) services[i]();
        vTaskDelay(1);
    }
    return NULL;
}
void protocol_test_start(void)
{
    assert(mcom_init() == 0 && pcore_init() == 0 && count == 2);
    assert(pthread_create(&thread, NULL, tick, NULL) == 0);
}
void protocol_test_stop(void)
{
    atomic_store(&stop, true);
    pthread_join(thread, NULL);
}
bool carmon_platform_ready(void) { return false; }
void carmon_request_restart(void) { assert(0); }
int mon_diag_enter(unsigned char c) { assert(0); return 0; }
int can_process_query(char *p, int n) { assert(0); return 0; }
int cm_process_query(char *p, int n) { assert(0); return 0; }
int obd_process_query(char *p, int n) { assert(0); return 0; }
BOOL ee24_initialized(void) { assert(0); return FALSE; }
int ee24_getpar(int a, int *b, unsigned char **c) { assert(0); return 0; }
int ee24_setpar(int a, int b, char *c) { assert(0); return 0; }
int pcf8563_gettime(struct tm *t) { assert(0); return 0; }
int pcf8563_settime(struct tm *t) { assert(0); return 0; }
int i2c_request_bus(void) { assert(0); return 0; }
int i2c_release_bus(void) { assert(0); return 0; }
int i2c_random_read(unsigned char a, unsigned char b, unsigned char *c, int d) { assert(0); return 0; }
int i2c_write(unsigned char a, unsigned char *b, int c) { assert(0); return 0; }
