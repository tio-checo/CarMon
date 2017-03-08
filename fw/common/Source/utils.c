/*********************************************************************
 *
 * Utilities
 *
 *********************************************************************/

#include <errno.h>
#include <sys/types.h>
#include <stddef.h>

#include "GenericTypeDefs.h"
#include "dma.h"

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
	hbz = _clz(in) / 4;
	hbn = (2 * sizeof (unsigned long)) - hbz;

	/*
	 * Adjust input value, so that it can be converted
	 * via processing the highest half byte.
	 */
	in <<= (hbz * 4);

	for (i = 0; i < hbn; i++) {
		if ((in & 0xf0000000UL) != 0) {
			out[written++] = hex2ascii_tbl[(in >> 28) & 0x0f];
		}

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
	unsigned int crc;

	/*
	 * Configure DMA module:
	 *  - use DMA channel 0 with priority 0
	 *  - CRC checksum is to be calculated
	 *  - input data reflected or not
	 *  - bytes not reordered
	 */
	/* Sets DRCCON */
	DmaSfmConfigure(DMA_CHKSUM_CRC, DMA_BITO_MSb, DMA_REORDER_NOT);
	/* Configure polynomial, its length and initial seed */
	DmaSfmCrcConfigure(0x1021, 16, 0x84cf);
	/* Blocks until DMA is done with CRC calc. */
	/* Assume the calculation always succeeds. */
	(void) DmaChnMemCrc(&crc, buf, ln, DMA_CHANNEL0, DMA_CHN_PRI0);

	return (unsigned short)(crc & 0xffff);
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
