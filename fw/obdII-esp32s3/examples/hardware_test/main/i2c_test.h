#ifndef I2C_TEST_H
#define I2C_TEST_H

#include <stdbool.h>

void i2c_test_run(void);
/* Set PCF8563 to system UTC; caller must first complete NTP sync. */
bool i2c_test_rtc_set_time(void);

#endif /* I2C_TEST_H */
