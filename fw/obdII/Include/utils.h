/*********************************************************************
 *
 * Utilities
 *
 *********************************************************************/

#ifndef __UTILS_H
#define __UTILS_H

/* Convert given number of bytes (max 4) to HEX ASCII string */
extern int hex2ascii(unsigned char *, unsigned char *, int);

/* convert given number of ascii hex characters (max 8) to unsigned long */
extern unsigned long ascii2hex(unsigned char *, int);

/* Convert given number of ascii hex characters to sequence of bytes */
extern int ascii2hexbuf(unsigned char *in, unsigned char *out, int ln);

#endif
