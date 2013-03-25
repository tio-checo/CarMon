/*********************************************************************
 *
 * Utilities
 *
 *********************************************************************/

#ifndef __UTILS_H
#define __UTILS_H

/*
 * Converts given number of bytes of unsigned int to ASCII HEX string.
 * Starts with MSB.
 */
extern int uint2hex(unsigned int in, char *out, int ln);

/* Converts given number of bytes to ASCII HEX string */
extern int ucharbuf2hex(unsigned char *, unsigned char *, int);

/* Converts given number of ASCII HEX characters (max 8) to unsigned long */
extern unsigned long hex2ulong(char *, int);

/* Converts given number of ASCII HEX characters to sequence of bytes */
extern int hex2ucharbuf(char *in, unsigned char *out, int ln);

#endif
