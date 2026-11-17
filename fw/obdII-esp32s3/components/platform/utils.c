/*********************************************************************
 *
 * Utilities
 *
 *********************************************************************/

#include <errno.h>
#include <sys/types.h>
#include <stddef.h>

#include "GenericTypeDefs.h"
#include "utils.h"

/* local constants */
/* HEX->ASCII conversion table */
static const char hex2ascii_tbl[16] = {'0', '1', '2', '3', '4', '5', '6',
    '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

/* look-up table for CRC calculation */
static const unsigned short crc_table[16] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef
};

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
 * Converts unsigned long to ASCII HEX string.
 * Leading zeros are not printed.
 * Returns number of written characters.
 */
int
ulong2hexwolz(unsigned long in, char *out)
{
	int i, hbz, hbn, written;

	written = 0;

	/* Treat zero as a special case, no need to loop. */
	if (in == 0) {
		out[0] = '0';
		return 1;
	}

	/*
	 * Count leading zeros to determine how many
	 * half bytes (bit quartets) are to be processed
	 * (leading zero half bytes are skipped).
	 */
	hbz = __builtin_clzl(in) / 4;
	hbn = (2 * sizeof (unsigned long)) - hbz;

	/*
	 * Adjust input value, so that it can be converted
	 * via processing the highest half byte.
	 */
	in <<= (hbz * 4);

	for (i = 0; i < hbn; i++) {
		out[written++] = hex2ascii_tbl[(in >> 28) & 0x0f];

		in <<= 4;
	}

	return written;
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
 * in and out can point to the same buffer in which case input data
 * are overwritten with the converted ones.
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
 * Stops conversion once non-digit character is found.
 */
unsigned long
dec2ulong(char *a, int ln)
{
	int i;
	unsigned long res = 0;

	if (ln > 10) {
		ln = 10;
	}

	for (i = 0; i < ln; i++) {
		if (a[i] < '0' || a[i] > '9') {
			break;
		}

		res *= 10;
		res += a[i] - '0';
	}

	return res;
}

/*
 * Converts unsigned long to string, optionally
 * with leading zeros.
 */
int
long2str(long n, char *out, int ln, BOOL lz)
{
	int i;
	BOOL display_minus;

	/* Preprocess negative numbers. */
	if (n < 0) {
		n = -n;
		display_minus = TRUE;
	} else {
		display_minus = FALSE;
	}

	for (i = ln - 1; i >= 0; i--) {
		/*
		 * If leading zeros are not to be displayed,
		 * display spaces instead.
		 */
		if (!lz && (n == 0) && (i != ln - 1)) {
			/*
			 * Display minus sign for negative number
			 * in place of the first space.
			 */
			if (display_minus) {
				out[i] = '-';

				display_minus = FALSE;
			} else {
				out[i] = ' ';
			}

			continue;
		}

		out[i] = '0' + n % 10;

		n /= 10;
	}

	return 0;
}

/*
 * Initializes 16bit CRC-CCITT calculation algorithm
 */
void
crc_init(unsigned short *crc, unsigned short seed)
{
	*crc = seed;
}

/*
 * Calculates 16bit CRC-CCITT
 */
void
crc_calc(unsigned short *crc, unsigned char data)
{
	unsigned short i;

	i = (*crc >> 12) ^ (data >> 4);
	*crc = crc_table[i & 0x0F] ^ (*crc << 4);
	i = (*crc >> 12) ^ (data >> 0);
	*crc = crc_table[i & 0x0F] ^ (*crc << 4);
}

/*
 * Finishes calculation of CRC-CCITT and returns resulting value.
 */
void
crc_finalize(unsigned short *crc)
{
	/*
	 * Per standard, empty messages should be appended
	 * with 16 zero bits. We do not handle that for now,
	 * as we do not deal with empty messages at all.
	 */
/*
	crc_calc(crc, 0);
	crc_calc(crc, 0);
*/
}

/*
 * Calculates 16-bit CRC-CCITT for given array of bytes using DMA
 */
unsigned short
crc_dma_calc_array(void *buf, int ln)
{
    /* PIC32 DMA used polynomial 0x1021, MSB first, seed 0x84cf.
     * Keep the API for EEPROM callers; no DMA peripheral is required. */
    return crc_calc_array(buf, ln, 0x84cf);
}

/*
 * Calculates 16-bit CRC-CCITT for given array of bytes using lookup table
 */
unsigned short
crc_calc_array(void *buf, int ln, unsigned short seed)
{
	unsigned short crc;
	int i;
	unsigned char *barray = (unsigned char *)buf;

	crc_init(&crc, seed);
	for (i = 0; i < ln; i++) {
		crc_calc(&crc, barray[i]);
	}

	crc_finalize(&crc);

	return crc;
}
/* ESP-IDF supplies memcpy/memset; do not override its libc. */
