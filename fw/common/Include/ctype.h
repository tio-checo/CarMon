#ifndef _CTYPE_H_
#if defined(__C32_VERSION__) || defined(__XC32)
#ifdef __cplusplus
extern "C" {
#endif
#endif
#define _CTYPE_H_

#if defined(__C30_0__)
extern int isalnum(int);
extern int isalpha(int);
extern int iscntrl(int);
extern int isdigit(int);
extern int isgraph(int);
extern int islower(int);
extern int isprint(int);
extern int ispunct(int);
extern int isspace(int);
extern int isupper(int);
extern int isxdigit(int);
extern int tolower(int);
extern int toupper(int);
#endif

#if defined(__C32_VERSION__) || defined(__XC32)
/* --- Constants --- */
#define _CT_U   0x01
#define _CT_L   0x02
#define _CT_N   0x04
#define _CT_S   0x08
#define _CT_P   0x10
#define _CT_C   0x20
#define _CT_X   0x40
#define _CT_SP  0x80

/* --- Prototypes --- */
int isalnum     (int c);
int isalpha     (int c);
int isascii     (int c);
int iscntrl     (int c);
int isdigit     (int c);
int isgraph     (int c);
int islower     (int c);
int isprint     (int c);
int ispunct     (int c);
int isspace     (int c);
int isupper     (int c);
int isxdigit    (int c);
int toascii     (int c);
int _tolower    (int c);
int _toupper    (int c);
int tolower     (int c);
int toupper     (int c);

/* --- Storage --- */
extern const char _ctype[];

/* --- Macros --- */
#define isalnum(c)      ((_ctype+1)[(unsigned)c] & (_CT_U|_CT_L|_CT_N))
#define isalpha(c)      ((_ctype+1)[(unsigned)c] & (_CT_U|_CT_L))
#define iscntrl(c)      ((_ctype+1)[(unsigned)c] & (_CT_C))
#define isdigit(c)      ((_ctype+1)[(unsigned)c] & (_CT_N))
#define isgraph(c)      ((_ctype+1)[(unsigned)c] & (_CT_U|_CT_L|_CT_N|_CT_P))
#define islower(c)      ((_ctype+1)[(unsigned)c] & (_CT_L))
#define isprint(c)      ((_ctype+1)[(unsigned)c] & (_CT_U|_CT_L|_CT_N|_CT_P|_CT_SP))
#define ispunct(c)      ((_ctype+1)[(unsigned)c] & (_CT_P))
#define isspace(c)      ((_ctype+1)[(unsigned)c] & (_CT_S|_CT_SP))
#define isupper(c)      ((_ctype+1)[(unsigned)c] & (_CT_U))
#define isxdigit(c)     ((_ctype+1)[(unsigned)c] & (_CT_N|_CT_X))

#define isascii(c)      ((unsigned)(c) <= 0x7f)
#define toascii(c)      ((c) & 0x7f)

#define _tolower(c)     ((c) - 'A' + 'a')
#define _toupper(c)     ((c) - 'a' + 'A')

#ifdef __cplusplus
}
#endif
#endif
#endif /* _CTYPE_H_ */
