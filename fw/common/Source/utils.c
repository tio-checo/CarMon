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

