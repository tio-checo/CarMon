#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_test.h"
#include "nvs.h"
#include "nvs_flash.h"

#define I2C_TEST_SDA		GPIO_NUM_2
#define I2C_TEST_SCL		GPIO_NUM_1
#define I2C_TEST_SPEED		100000
#define I2C_TEST_TIMEOUT	100
#define I2C_TEST_EEPROM_ADDR	0x54
#define I2C_TEST_RTC_ADDR	0x51

/* Both devices use a one-byte address pointer before a repeated START. */
static esp_err_t
i2c_test_read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data,
    size_t size)
{
	return (i2c_master_transmit_receive(dev, &reg, 1, data, size,
	    I2C_TEST_TIMEOUT));
}

/* Claim the attempt in ESP flash before touching the external EEPROM. */
static bool
i2c_test_eeprom_write_once(i2c_master_dev_handle_t dev, bool *written)
{
	nvs_handle_t nvs;
	esp_err_t err;
	uint8_t attempted, data[2];
	unsigned int offset;

	*written = false;
	err = nvs_flash_init();
	if (err != ESP_OK) {
		printf("EEPROM FAIL: NVS init: %s\n", esp_err_to_name(err));
		return (false);
	}
	err = nvs_open("hw_test", NVS_READWRITE, &nvs);
	if (err != ESP_OK) {
		printf("EEPROM FAIL: NVS open: %s\n", esp_err_to_name(err));
		return (false);
	}
	err = nvs_get_u8(nvs, "eep_addr_once", &attempted);
	if (err == ESP_OK) {
		nvs_close(nvs);
		printf("EEPROM: write attempt already recorded; "
		    "skipping all data writes.\n");
		return (true);
	}
	if (err == ESP_ERR_NVS_NOT_FOUND) {
		err = nvs_set_u8(nvs, "eep_addr_once", 1);
		if (err == ESP_OK)
			err = nvs_commit(nvs);
	}
	nvs_close(nvs);
	if (err != ESP_OK) {
		printf("EEPROM FAIL: persistent write guard: %s\n",
		    esp_err_to_name(err));
		return (false);
	}

	printf("EEPROM: one-time address pattern write, 256 byte writes.\n");
	for (offset = 0; offset < 256; offset++) {
		data[0] = offset;
		data[1] = offset;
		err = i2c_master_transmit(dev, data, sizeof (data),
		    I2C_TEST_TIMEOUT);
		if (err != ESP_OK) {
			printf("EEPROM FAIL: write at 0x%02X: %s; "
			    "no retry.\n", offset, esp_err_to_name(err));
			return (false);
		}
		/* tW is at most 5 ms; include a tick for rounding/phase. */
		vTaskDelay(pdMS_TO_TICKS(10) + 1);
	}
	*written = true;
	return (true);
}

static bool
i2c_test_eeprom(i2c_master_dev_handle_t dev)
{
	uint8_t first[256], second[256];
	esp_err_t err;
	unsigned int offset, column;
	bool written;

	if (!i2c_test_eeprom_write_once(dev, &written))
		return (false);

	/* Send only the address pointer: never send EEPROM data to write. */
	for (offset = 0; offset < sizeof (first); offset += 32) {
		err = i2c_test_read(dev, offset, first + offset, 32);
		if (err == ESP_OK)
			err = i2c_test_read(dev, offset, second + offset, 32);
		if (err != ESP_OK) {
			printf("EEPROM FAIL: read at 0x%02x: %s\n", offset,
			    esp_err_to_name(err));
			return (false);
		}
	}
	if (memcmp(first, second, sizeof (first)) != 0) {
		printf("EEPROM FAIL: repeated reads differ.\n");
		return (false);
	}
	/* Dump the stable snapshot without issuing additional I2C reads. */
	printf("EEPROM contents (hex):\n");
	for (offset = 0; offset < sizeof (first); offset += 16) {
		printf("%04X:", offset);
		for (column = 0; column < 16; column++)
			printf(" %02X", (unsigned int)first[offset + column]);
		printf("\n");
	}
	for (offset = 0; offset < sizeof (first); offset++) {
		if (first[offset] != (uint8_t)offset) {
			printf("EEPROM FAIL: address=0x%02X, expected=%02X, "
			    "read=%02X; no rewrite.\n", offset, offset,
			    (unsigned int)first[offset]);
			return (false);
		}
	}
	printf("EEPROM PASS: 256 address values verified twice; "
	    "%s.\n", written ? "each byte written once" :
	    "no data written this boot");
	return (true);
}

static int
i2c_test_bcd(uint8_t value, unsigned int min, unsigned int max)
{
	unsigned int number = (value >> 4) * 10 + (value & 0x0f);

	if ((value & 0x0f) > 9 || (value >> 4) > 9 ||
	    number < min || number > max)
		return (-1);
	return (number);
}

static bool
i2c_test_rtc(i2c_master_dev_handle_t dev)
{
	uint8_t first[9], second[9];
	esp_err_t err;
	int sec, min, hour, day, month, year, next, delta;

	err = i2c_test_read(dev, 0, first, sizeof (first));
	if (err != ESP_OK) {
		printf("RTC FAIL: register read: %s\n", esp_err_to_name(err));
		return (false);
	}

	/* Do not clear VL, change STOP or set the time during this test. */
	sec = i2c_test_bcd(first[2] & 0x7f, 0, 59);
	min = i2c_test_bcd(first[3] & 0x7f, 0, 59);
	hour = i2c_test_bcd(first[4] & 0x3f, 0, 23);
	day = i2c_test_bcd(first[5] & 0x3f, 1, 31);
	month = i2c_test_bcd(first[7] & 0x1f, 1, 12);
	year = i2c_test_bcd(first[8], 0, 99);
	printf("RTC: control1=0x%02x, control2=0x%02x, VL=%u, STOP=%u\n",
	    first[0], first[1], !!(first[2] & 0x80), !!(first[0] & 0x20));
	if (sec >= 0 && min >= 0 && hour >= 0 && day >= 0 &&
	    month >= 0 && year >= 0 && (first[6] & 7) <= 6) {
		printf("RTC: day=%02d month=%02d year=%02d century-bit=%u, "
		    "%02d:%02d:%02d\n", day, month, year,
		    !!(first[7] & 0x80), hour, min, sec);
	} else {
		printf("RTC WARNING: invalid calendar fields; "
		    "time may not be initialized.\n");
	}
	if (first[2] & 0x80)
		printf("RTC WARNING: VL set; stored time is not reliable.\n");
	if (first[0] & 0x20)
		printf("RTC WARNING: STOP set; clock is stopped.\n");

	vTaskDelay(pdMS_TO_TICKS(1200));
	err = i2c_test_read(dev, 0, second, sizeof (second));
	if (err != ESP_OK) {
		printf("RTC FAIL: second read: %s\n", esp_err_to_name(err));
		return (false);
	}
	printf("RTC PASS: address=0x51, control/calendar registers "
	    "read twice (no settings changed).\n");
	next = i2c_test_bcd(second[2] & 0x7f, 0, 59);
	delta = (next - sec + 60) % 60;
	if (sec >= 0 && next >= 0 && !(first[0] & 0x20) &&
	    !(second[0] & 0x20) && delta >= 1 && delta <= 2) {
		printf("RTC TICK PASS: seconds advanced by %d.\n", delta);
	} else {
		printf("RTC TICK WARNING: expected 1-2 seconds advance; "
		    "seconds=%d -> %d. Check STOP/time/crystal.\n", sec, next);
	}
	return (true);
}

static uint8_t
i2c_test_to_bcd(unsigned int value)
{
	return (((value / 10) << 4) | (value % 10));
}

/* Called by the Wi-Fi task after NTP, after the startup bus is released. */
bool
i2c_test_rtc_set_time(void)
{
	i2c_master_bus_handle_t bus;
	i2c_master_dev_handle_t dev = NULL;
	i2c_master_bus_config_t bus_config = {
		.i2c_port = I2C_NUM_0,
		.sda_io_num = I2C_TEST_SDA,
		.scl_io_num = I2C_TEST_SCL,
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = 7,
		.flags.enable_internal_pullup = false,
	};
	i2c_device_config_t dev_config = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = I2C_TEST_RTC_ADDR,
		.scl_speed_hz = I2C_TEST_SPEED,
	};
	const uint8_t masks[7] = { 0xff, 0x7f, 0x3f, 0x3f,
	    0x07, 0x9f, 0xff };
	uint8_t control[2] = { 0, 0x20 };
	uint8_t data[8], check[9];
	struct tm utc;
	time_t now;
	esp_err_t err, restart_err;
	bool passed = false;
	unsigned int i;
	int second, delta;

	err = i2c_new_master_bus(&bus_config, &bus);
	if (err != ESP_OK) {
		printf("RTC SET FAIL: bus: %s\n", esp_err_to_name(err));
		return (false);
	}
	err = i2c_master_bus_add_device(bus, &dev_config, &dev);
	if (err != ESP_OK)
		goto out;

	now = time(NULL);
	if (gmtime_r(&now, &utc) == NULL || utc.tm_year < 100 ||
	    utc.tm_year > 199 || utc.tm_sec > 59) {
		printf("RTC SET FAIL: UTC outside supported years 2000-2099 "
		    "or leap second.\n");
		goto out;
	}
	/* Write seconds through years together; C=0 means 2000-2099. */
	data[0] = 2;
	data[1] = i2c_test_to_bcd(utc.tm_sec); /* Also clear VL. */
	data[2] = i2c_test_to_bcd(utc.tm_min);
	data[3] = i2c_test_to_bcd(utc.tm_hour);
	data[4] = i2c_test_to_bcd(utc.tm_mday);
	data[5] = utc.tm_wday;
	data[6] = i2c_test_to_bcd(utc.tm_mon + 1);
	data[7] = i2c_test_to_bcd(utc.tm_year % 100);

	/* Freeze counters and select normal mode (TEST1=TESTC=0). */
	err = i2c_master_transmit(dev, control, sizeof (control),
	    I2C_TEST_TIMEOUT);
	if (err == ESP_OK)
		err = i2c_master_transmit(dev, data, sizeof (data),
		    I2C_TEST_TIMEOUT);
	if (err == ESP_OK)
		err = i2c_test_read(dev, 0, check, sizeof (check));
	if (err == ESP_OK) {
		if (!(check[0] & 0x20))
			err = ESP_ERR_INVALID_RESPONSE;
		for (i = 0; i < 7; i++) {
			if ((check[i + 2] & masks[i]) != data[i + 1])
				err = ESP_ERR_INVALID_RESPONSE;
		}
	}

	/* Always try to release STOP, including after an I2C error. */
	control[1] = 0;
	restart_err = i2c_master_transmit(dev, control, sizeof (control),
	    I2C_TEST_TIMEOUT);
	if (restart_err != ESP_OK) {
		printf("RTC SET FAIL: restart: %s; RTC may be stopped.\n",
		    esp_err_to_name(restart_err));
		goto out;
	}
	if (err != ESP_OK)
		goto out;

	vTaskDelay(pdMS_TO_TICKS(1200));
	err = i2c_test_read(dev, 0, check, sizeof (check));
	if (err != ESP_OK)
		goto out;
	second = i2c_test_bcd(check[2] & 0x7f, 0, 59);
	delta = (second - utc.tm_sec + 60) % 60;
	if ((check[0] & 0xa8) || (check[2] & 0x80) || second < 0 ||
	    delta < 1 || delta > 2) {
		printf("RTC SET FAIL: clock not running normally or VL set.\n");
		goto out;
	}
	passed = true;
out:
	if (err != ESP_OK)
		printf("RTC SET FAIL: I2C/write verification: %s\n",
		    esp_err_to_name(err));
	if (dev != NULL) {
		err = i2c_master_bus_rm_device(dev);
		if (err != ESP_OK) {
			printf("RTC SET FAIL: device cleanup: %s\n",
			    esp_err_to_name(err));
			passed = false;
		}
	}
	err = i2c_del_master_bus(bus);
	if (err != ESP_OK) {
		printf("RTC SET FAIL: bus cleanup: %s\n", esp_err_to_name(err));
		passed = false;
	}
	if (passed)
		printf("RTC SET PASS: %04d-%02d-%02d %02d:%02d:%02d UTC, "
		    "readback verified, VL=0, clock running.\n",
		    utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
		    utc.tm_hour, utc.tm_min, utc.tm_sec);
	return (passed);
}

static bool
i2c_test_device(i2c_master_bus_handle_t bus, uint8_t address)
{
	i2c_master_dev_handle_t dev;
	i2c_device_config_t config = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = address,
		.scl_speed_hz = I2C_TEST_SPEED,
	};
	esp_err_t err;
	bool passed;

	err = i2c_master_bus_add_device(bus, &config, &dev);
	if (err != ESP_OK) {
		printf("I2C FAIL: device 0x%02x: %s\n", address,
		    esp_err_to_name(err));
		return (false);
	}
	if (address == I2C_TEST_EEPROM_ADDR)
		passed = i2c_test_eeprom(dev);
	else
		passed = i2c_test_rtc(dev);
	err = i2c_master_bus_rm_device(dev);
	if (err != ESP_OK) {
		printf("I2C FAIL: remove device: %s\n", esp_err_to_name(err));
		passed = false;
	}
	return (passed);
}

void
i2c_test_run(void)
{
	i2c_master_bus_handle_t bus;
	i2c_master_bus_config_t config = {
		.i2c_port = I2C_NUM_0,
		.sda_io_num = I2C_TEST_SDA,
		.scl_io_num = I2C_TEST_SCL,
		.clk_source = I2C_CLK_SRC_DEFAULT,
		.glitch_ignore_cnt = 7,
		.flags.enable_internal_pullup = false,
	};
	bool present[128] = { false };
	bool eeprom = false, rtc = false;
	esp_err_t err;
	unsigned int address, count = 0;

	printf("\nI2C: SDA=GPIO2, SCL=GPIO1, 100 kHz, external pull-ups.\n");
	err = i2c_new_master_bus(&config, &bus);
	if (err != ESP_OK) {
		printf("I2C FAIL: initialization: %s\n", esp_err_to_name(err));
		return;
	}
	/* Probe usable 7-bit addresses without sending device data. */
	for (address = 0x08; address <= 0x77; address++) {
		err = i2c_master_probe(bus, address, I2C_TEST_TIMEOUT);
		if (err == ESP_OK) {
			present[address] = true;
			count++;
			printf("I2C: ACK at 0x%02x\n", address);
		} else if (err != ESP_ERR_NOT_FOUND) {
			printf("I2C FAIL: scan at 0x%02x: %s; "
			    "check wiring/pull-ups/power.\n", address,
			    esp_err_to_name(err));
			goto out;
		}
	}
	printf("I2C: scan complete, %u responding addresses.\n", count);
	if (present[I2C_TEST_EEPROM_ADDR])
		eeprom = i2c_test_device(bus, I2C_TEST_EEPROM_ADDR);
	else
		printf("EEPROM FAIL: no ACK at expected address 0x54.\n");
	if (present[I2C_TEST_RTC_ADDR])
		rtc = i2c_test_device(bus, I2C_TEST_RTC_ADDR);
	else
		printf("RTC FAIL: no ACK at expected address 0x51.\n");
out:
	err = i2c_del_master_bus(bus);
	if (err != ESP_OK)
		printf("I2C FAIL: bus cleanup: %s\n", esp_err_to_name(err));
	printf("I2C RESULT: EEPROM=%s, RTC communication=%s\n\n",
	    eeprom ? "PASS" : "FAIL", rtc ? "PASS" : "FAIL");
}
