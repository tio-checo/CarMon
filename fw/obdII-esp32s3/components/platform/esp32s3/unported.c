/* Explicit build-stage adapters. No function here emulates successful I/O.
 * Replace these by real drivers one peripheral at a time. */
#include <errno.h>
#include "carmon_platform.h"
#include "carmon_media.h"
#include "bt.h"
#include "can_hal.h"
#include "i2c_hal.h"
#include "mon.h"
#include "usb_app.h"

bool carmon_platform_ready(void) { return false; }
int can_init(void) { return ENOTSUP; }
int can_suspend(void) { return ENOTSUP; }
int can_process_query(char *p, int n) { return ENOTSUP; }
int can_send_data_response_to_master(char *a, char *b) { return ENOTSUP; }
BOOL can_response_inuse(void) { return FALSE; }
int can_configure(int a, int b, int c, UINT32 d, UINT32 e, BOOL f) { return ENOTSUP; }
BOOL can_configuration_in_progress(void) { return FALSE; }
int can_obd_send(unsigned char *p, int n) { return ENOTSUP; }
int can_obd_recv(unsigned char **p, int *n) { return ENOTSUP; }
/* int i2c_init(void) { return ENOTSUP; } */
int i2c_request_bus(void) { return ENOTSUP; }
int i2c_release_bus(void) { return ENOTSUP; }
int i2c_write(unsigned char a, unsigned char *p, int n) { return ENOTSUP; }
int i2c_current_read(unsigned char a, unsigned char *p, int n) { return ENOTSUP; }
int i2c_random_read(unsigned char a, unsigned char r, unsigned char *p, int n) { return ENOTSUP; }
int bt_init(void) { return ENOTSUP; }
int bt_suspend(void) { return ENOTSUP; }
int bt_putchar(unsigned char c, BOOL b) { return ENOTSUP; }
int bt_getchar_gps(unsigned char *c) { return ENOTSUP; }
int bt_getchar_master(unsigned char *c) { return ENOTSUP; }
BOOL bt_is_paired(void) { return FALSE; }
BOOL bt_is_paired_with_gps(void) { return FALSE; }
BOOL bt_is_paired_with_master(void) { return FALSE; }
/*
int mon_init(void) { return ENOTSUP; }
int mon_svc(void) { return ENOTSUP; }
int mon_diag_enter(unsigned char c) { return ENOTSUP; }
int mon_diag_leave(void) { return ENOTSUP; }
BOOL mon_is_diag_mode(void) { return FALSE; }
*/
int uapp_init(void) { return ENOTSUP; }
int uapp_svc(void) { return ENOTSUP; }
int usbsacm_getchar(unsigned char *c) { return ENOTSUP; }
int usbsacm_putchar(unsigned char c, BOOL b) { return ENOTSUP; }
int usbsacm_putchars(unsigned char c, BOOL b) { return ENOTSUP; }
int carmon_gps_uart_init(void) { return ENOTSUP; }
int carmon_gps_uart_read(unsigned char *c) { return ENOTSUP; }
int carmon_gps_uart_suspend(void) { return ENOTSUP; }
void carmon_media_init(void) { }
bool carmon_media_present(void) { return false; }
bool carmon_media_read_sector(uint64_t s, uint8_t *p) { return false; }
bool carmon_media_write_sector(uint64_t s, const void *p, bool a) { return false; }
uint64_t carmon_media_capacity(void) { return 0; }
