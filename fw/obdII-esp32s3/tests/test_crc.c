/* Host regression vectors for the portable CRC used by DLFS and EEPROM.
 * Run: cc -Icomponents/platform/include tests/test_crc.c components/platform/utils.c -o /tmp/carmon-crc-test && /tmp/carmon-crc-test
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "utils.h"

/* Bit-by-bit reference, independent of the nibble lookup implementation. */
static uint16_t reference(const unsigned char *p, int n, uint16_t crc)
{
    while (n--) {
        crc ^= (uint16_t)*p++ << 8;
        for (int i = 0; i < 8; ++i)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return crc;
}

int main(void)
{
    unsigned char data[512];
    char digits[] = "123456789";
    assert(crc_calc_array(digits, 9, 0xffff) == 0x29b1);
    assert(crc_calc_array(digits, 9, 0) == 0x31c3);
    for (int i = 0; i < (int)sizeof(data); ++i) data[i] = (unsigned char)(i * 37 + 11);
    const int lengths[] = {0, 1, 2, 16, 62, 64, 126, 128, 510, 512};
    for (unsigned i = 0; i < sizeof(lengths)/sizeof(lengths[0]); ++i) {
        int n = lengths[i];
        assert(crc_calc_array(data, n, 0xffff) == reference(data, n, 0xffff));
        assert(crc_dma_calc_array(data, n) == reference(data, n, 0x84cf));
    }
    puts("CRC PASS: standard vectors, DLFS sector sizes and EEPROM CRC seed");
    return 0;
}
