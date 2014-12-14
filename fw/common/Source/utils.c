/*********************************************************************
 *
 * Utilities
 *
 *********************************************************************/

#include <errno.h>

/* local constants */
/* HEX->ASCII conversion table */
static const char hex2ascii_tbl[16] = {'0', '1', '2', '3', '4', '5', '6',
    '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

/* look-up table for CRC calculation */
static const unsigned short crc_table[16] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef
};

/* local variables */
/* crc checksum */
static unsigned short crc;

/*
 * Converts given number of bytes of unsigned int to ASCII HEX string.
 * Start with MSB.
 */
int
uint2hex(unsigned int in, char *out, int ln)
{
	int i;

	for (i = 0; i < ln; i++) {
		out[i * 2] = hex2ascii_tbl[(in >> (8 * (ln - 1 - i) + 4)) & 0x0f];
		out[i * 2 + 1] = hex2ascii_tbl[(in >> (8 * (ln - 1 - i))) & 0x0f];
	}

	return 0;
}

/*
 * Converts given number of bytes to ASCII HEX string
 */
int
ucharbuf2hex(unsigned char *in, unsigned char *out, int ln)
{
	int i;

	for (i = 0; i < ln; i++) {
		out[i * 2] = hex2ascii_tbl[(in[i] >> 4) & 0x0f];
		out[i * 2 + 1] = hex2ascii_tbl[in[i] & 0x0f];
	}

	return 0;
}

/*
 * Converts given number of ASCII HEX characters (max 8) to unsigned long
 */
unsigned long
hex2ulong(char *a, int ln)
{
	int i;
	unsigned long res = 0;

	if (ln > 8)
		return EINVAL;

	for (i = 0; i < ln; i++) {
		res <<= 4;
		res |= a[i] <= '9' ? a[i] - '0' : (a[i] & ~0x20) - 'A' + 10;
	}

	return res;
}

/*
 * Converts given number of ASCII HEX characters to sequence of bytes
 */
int
hex2ucharbuf(char *in, unsigned char *out, int ln)
{
	int i;

	/* length must be even number */
	if (ln % 2 != 0)
		return EINVAL;

	for (i = 0; i < ln / 2; i++) {
		out[i] = hex2ulong(&in[i * 2], 2);
	}

	return 0;
}

/*
 * Converts given number of ASCII decimal characters (max 10) to unsigned long
 */
unsigned long
dec2ulong(char *a, int ln)
{
	int i;
	unsigned long res = 0;

	if (ln > 10)
		return 0;

	for (i = 0; i < ln; i++) {
		res *= 10;
		if (a[i] < '0' || a[i] > '9')
			return 0;

		res += a[i] - '0';
	}

	return res;
}

/*
 * Initializes 16bit CRC-CCITT calculation algorithm
 */
void
crc_init(void)
{
	crc = 0xffff;
}

/*
 * Calculates 16bit CRC-CCITT
 */
void
crc_calc(unsigned char data)
{
	unsigned short i;

	i = (crc >> 12) ^ (data >> 4);
	crc = crc_table[i & 0x0F] ^ (crc << 4);
	i = (crc >> 12) ^ (data >> 0);
	crc = crc_table[i & 0x0F] ^ (crc << 4);
}

/*
 * Finishes calculation of CRC-CCITT and returns resulting value.
 */
unsigned short
crc_finalize(void)
{
	/*
	 * Per standard, empty messages should be appended
	 * with 16 zero bits. We do not handle that for now,
	 * as we do not deal with empty messages at all.
	 */
/*
	crc_calc(0);
	crc_calc(0);
*/
	return crc;
}
