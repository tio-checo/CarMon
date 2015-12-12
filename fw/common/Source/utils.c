/*********************************************************************
 *
 * Utilities
 *
 *********************************************************************/

#include <errno.h>
#include <sys/types.h>
#include <stddef.h>

#include "GenericTypeDefs.h"

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

	/* Display minus sign for negative numbers. */
	display_minus = n < 0;

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

#define op_t unsigned long int
#define OPSIZ (sizeof (op_t))
typedef unsigned char byte;
/*
 * Optimized version of memset
 */
void *
memset(void *dstpp, int c, size_t len)
{
	long int dstp = (long int) dstpp;

	if (len >= 8) {
		size_t xlen;
		op_t cccc;

		cccc = (unsigned char) c;
		cccc |= cccc << 8;
		cccc |= cccc << 16;

		/*
		 * There are at least some bytes to set.
		 */
		while (dstp % OPSIZ != 0) {
			((byte *) dstp)[0] = c;
			dstp += 1;
			len -= 1;
		}

		/*
		 * Write 8 `op_t' per iteration until less
		 * than 8 `op_t' remain.
		 */
		xlen = len / (OPSIZ * 8);
		while (xlen > 0) {
			((op_t *) dstp)[0] = cccc;
			((op_t *) dstp)[1] = cccc;
			((op_t *) dstp)[2] = cccc;
			((op_t *) dstp)[3] = cccc;
			((op_t *) dstp)[4] = cccc;
			((op_t *) dstp)[5] = cccc;
			((op_t *) dstp)[6] = cccc;
			((op_t *) dstp)[7] = cccc;
			dstp += 8 * OPSIZ;
			xlen -= 1;
		}
		len %= OPSIZ * 8;

		/*
		 * Write 1 `op_t' per iteration until less than
		 * OPSIZ bytes remain.
		 */
		xlen = len / OPSIZ;
		while (xlen > 0) {
			((op_t *) dstp)[0] = cccc;
			dstp += OPSIZ;
			xlen -= 1;
		}
		len %= OPSIZ;
	}

	/* Write the last few bytes.  */
	while (len > 0) {
		((byte *) dstp)[0] = c;
		dstp += 1;
		len -= 1;
	}

	return dstpp;
}

/*
 * Optimized version of memcpy
 */
void *
memcpy(void *dstpp, const void *srcpp, size_t len)
{
	long int dstp = (long int) dstpp;
	long int srcp = (long int) srcpp;

	/*
	 * Unaligned source or destinations pointers,
	 * need to copy byte-by-byte. Though unroll
	 * the loop a bit to speed up the process.
	 */
	if ((srcp % OPSIZ != 0) || (dstp % OPSIZ != 0)) {
		while (len >= 8) {
			((byte *) dstp)[0] = ((byte *)srcp)[0];
			((byte *) dstp)[1] = ((byte *)srcp)[1];
			((byte *) dstp)[2] = ((byte *)srcp)[2];
			((byte *) dstp)[3] = ((byte *)srcp)[3];
			((byte *) dstp)[4] = ((byte *)srcp)[4];
			((byte *) dstp)[5] = ((byte *)srcp)[5];
			((byte *) dstp)[6] = ((byte *)srcp)[6];
			((byte *) dstp)[7] = ((byte *)srcp)[7];

			dstp += 8;
			srcp += 8;

			len -= 8;
		}

		while (len > 0) {
			((byte *) dstp)[0] = ((byte *)srcp)[0];
			dstp += 1;
			srcp += 1;
			len -= 1;
		}

		return dstpp;
	}

	if (len >= 8) {
		size_t xlen;

		/*
		 * There are at least some bytes to copy.
		 */
		while (dstp % OPSIZ != 0) {
			((byte *) dstp)[0] = ((byte *)srcp)[0];
			dstp += 1;
			srcp += 1;
			len -= 1;
		}

		/*
		 * Copy 8 `op_t' per iteration until less
		 * than 8 `op_t' remain.
		 */
		xlen = len / (OPSIZ * 8);
		while (xlen > 0) {
			((op_t *) dstp)[0] = ((op_t *) srcp)[0];
			((op_t *) dstp)[1] = ((op_t *) srcp)[1];
			((op_t *) dstp)[2] = ((op_t *) srcp)[2];
			((op_t *) dstp)[3] = ((op_t *) srcp)[3];
			((op_t *) dstp)[4] = ((op_t *) srcp)[4];
			((op_t *) dstp)[5] = ((op_t *) srcp)[5];
			((op_t *) dstp)[6] = ((op_t *) srcp)[6];
			((op_t *) dstp)[7] = ((op_t *) srcp)[7];
			dstp += 8 * OPSIZ;
			srcp += 8 * OPSIZ;
			xlen -= 1;
		}
		len %= OPSIZ * 8;

		/*
		 * Write 1 `op_t' per iteration until less than
		 * OPSIZ bytes remain.
		 */
		xlen = len / OPSIZ;
		while (xlen > 0) {
			((op_t *) dstp)[0] = ((op_t *) srcp)[0];
			dstp += OPSIZ;
			srcp += OPSIZ;
			xlen -= 1;
		}
		len %= OPSIZ;
	}

	/* Write the last few bytes.  */
	while (len > 0) {
		((byte *) dstp)[0] = ((byte *) srcp)[0];
		dstp += 1;
		srcp += 1;
		len -= 1;
	}

	return dstpp;
}
