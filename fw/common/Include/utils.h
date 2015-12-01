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
extern int uint2hex(unsigned int, char *, int);

/* Converts given number of bytes to ASCII HEX string */
extern int ucharbuf2hex(unsigned char *, unsigned char *, int);

/* Converts given number of ASCII HEX characters (max 8) to unsigned long */
extern unsigned long hex2ulong(char *, int);

/* Converts given number of ASCII HEX characters to sequence of bytes */
extern int hex2ucharbuf(char *, unsigned char *, int);

/* Converts given number of ASCII decimal characters (max 10) to unsigned long */
extern unsigned long dec2ulong(char *, int);

/* Converts unsigned long to string with leading zeros. */
extern int ulong2str(unsigned long n, char *out, int ln);

/* Initializes CRC calculation algorithm */
extern void crc_init(void);

/* Calculates 16bit CRC */
extern void crc_calc(unsigned char);

/* Returns calculated CRC */
extern unsigned short crc_finalize(void);

#endif /* __UTILS_H */
