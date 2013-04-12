#ifndef	_STDDEF_H_
#define	_STDDEF_H_

typedef	__PTRDIFF_TYPE__ ptrdiff_t;	/* result type of pointer difference */
typedef	__SIZE_TYPE__ size_t;		/* type yielded by sizeof */
typedef __WCHAR_TYPE__ wchar_t;		/* wide char type */

#if !defined(offsetof) && (((__GNUC__ == 4) && (5 >= __GNUC_MINOR__)) || (__GNUC__ > 4))
/* Offset of member MEMBER in a struct of type TYPE. */
#define offsetof(TYPE, MEMBER) __builtin_offsetof (TYPE, MEMBER)
#else
#define	offsetof(ty, mem)	((int)&(((ty *)0)->mem))
#endif

#ifndef	NULL
#ifdef __GNUG__
#define NULL __null
#else   /* G++ */
#ifndef __cplusplus
#define NULL (0L)
#else   /* C++ */
#ifdef (__XC32)
#define NULL (0L)
#else
#define NULL (0)
#endif  /* XC32 */
#endif  /* C++ */
#endif  /* G++ */
#endif	/* NULL */

extern int	errno;			/* system error number */

#endif	/* _STDDEF_H_ */
