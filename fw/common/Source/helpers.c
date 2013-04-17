/*********************************************************************
 *
 * Helper functions
 *
 *********************************************************************/

#include "GenericTypeDefs.h"

#include <sys/types.h>
#include <sys/time.h>
#include <time.h>

#include "utils.h"

/* local variables */
/* local CPU time - updated from timer interrupt */
static struct timeval tv_cpu;

/* dynamic memory allocation not utilized for now */
#if 0
/* _end is set in the linker command file */
extern caddr_t _end;

/* just in case, most boards have at least some memory */
#ifndef RAMSIZE
#define RAMSIZE             0x100UL
#endif

/* types */
typedef struct sbrk_alloc_req {
	int size;	/* size of allocated memory */
	BOOL succeeded;	/* TRUE if allocation succeded */
} sbrk_alloc_req_t;

/*
 * array of structures holding information about allocated heap.
 * Keep information about five last allocations.
 */
static sbrk_alloc_req_t	sbrk_alloc_req_array[5];
/* total number of allocations */
static int sbrk_alloc_req_total;

/*
 * sbrk -- changes heap size. Get nbytes more RAM.
 *         We just increment a pointer in what's
 *         left of memory on the board.
 */
caddr_t
sbrk(int nbytes)
{
	static caddr_t heap_ptr = NULL;
	caddr_t        base;
	int i;

	if (heap_ptr == NULL) {
		heap_ptr = (caddr_t)&_end;
	}

	/* calculate index to allocation array */
	i = sbrk_alloc_req_total % 5;
	/* increment total number of allocations */
	sbrk_alloc_req_total++;

	/* record allocation request */
	sbrk_alloc_req_array[i].size = nbytes;

	if (((unsigned long)heap_ptr + (unsigned long)nbytes) <
	    (RAMSIZE + (unsigned long)&_end)) {
		base = heap_ptr;
		heap_ptr += nbytes;

		sbrk_alloc_req_array[i].succeeded = TRUE;

		return (base);
	} else {
/*    errno = ENOMEM; */
		sbrk_alloc_req_array[i].succeeded = FALSE;

		return ((caddr_t)-1);
	}
}

/*
 * print information about allocation requests
 */
int
print_alloc_requests(int (*printstr)())
{
	int i, j;
	char s[9];

	s[8] = '\0';

	uint2hex(sbrk_alloc_req_total, s, 4);
	printstr("Total number of requests: 0x");
	printstr(s);
	printstr("\n\r");

	/* No allocations were done, so return */
	if (sbrk_alloc_req_total == 0)
		return 0;

	for (i = 0, j = (sbrk_alloc_req_total - 1) % 5;
	    i < 5 && j < sbrk_alloc_req_total; i++, j++) {
		uint2hex(sbrk_alloc_req_array[j % 5].size, s, 4);

		printstr(" 0x");
		printstr(s);
		printstr(", [");
		if (sbrk_alloc_req_array[j % 5].succeeded)
			printstr("Y]\n\r");
		else
			printstr("N]\n\r");
	}

	return 0;
}
#endif

/*
 * updatetimeofday()
 */
int
updatetimeofday(time_t us)
{
	tv_cpu.tv_usec += us;

	/* Handle overflow */
	while (tv_cpu.tv_usec > 1000000) {
		tv_cpu.tv_usec -= 1000000;
		tv_cpu.tv_sec++;
	}

	return 0;
}

/*
 * time()
 */
time_t time(time_t *tod)
{
	if (tod != NULL)
		*tod = tv_cpu.tv_sec;

	return tv_cpu.tv_sec;
}

/*
 * settimeofday()
 */
int
settimeofday(const struct timeval *tv, void *tz)
{
	tv_cpu.tv_usec = tv->tv_usec;
	tv_cpu.tv_sec = tv->tv_sec;

	return 0;
}

