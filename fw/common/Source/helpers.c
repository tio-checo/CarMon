/*********************************************************************
 *
 * Helper functions
 *
 *********************************************************************/

#include "GenericTypeDefs.h"
#include "HardwareProfile.h"
#include "xc.h"

#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>

#include "helpers.h"
#include "rt_light.h"
#include "utils.h"

/* local variables */
/* local CPU time - updated from timer interrupt */
static volatile struct timeval tv_cpu;

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
	while (tv_cpu.tv_usec >= US_PER_SECOND) {
		tv_cpu.tv_usec -= US_PER_SECOND;
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
 * get_curr_hrtime()
 * 
 * Return current time with us resolution.
 *
 */
int
get_curr_hrtime(struct timeval *hrtod)
{
/*	unsigned long t1, t2; */

	if (hrtod == NULL)
		return EINVAL;

	/*
	 * This function may be called outside of interrupt context,
	 * so time can be updated while we are reading it. Account for that.
	 */
	do {
		hrtod->tv_usec = tv_cpu.tv_usec;
		hrtod->tv_sec = tv_cpu.tv_sec;
	} while (hrtod->tv_sec != tv_cpu.tv_sec);

	return 0;
}

/*
 * get_diff_hrtime()
 * 
 * Return difference between current time and specified time with us resolution.
 *
 */
int
get_diff_hrtime(struct timeval *hrt, struct timeval *hrdiff)
{
	struct timeval chrt;

	/* Get current time. */
	if (get_curr_hrtime(&chrt) != 0)
		return EINVAL;

	/*
	 * Adjust microseconds to simplify further comparison and calculation
	 * of difference.
	 */
	while ((hrt->tv_usec > chrt.tv_usec) && (chrt.tv_sec > 0)) {
		chrt.tv_usec += US_PER_SECOND;
		chrt.tv_sec--;
	}

	/* Return with error if current time is older than given time. */
	if (hrt->tv_sec > chrt.tv_sec)
		return EINVAL;

	/* Calculate the difference. */
	hrdiff->tv_usec = chrt.tv_usec - hrt->tv_usec;
	hrdiff->tv_sec = chrt.tv_sec - hrt->tv_sec;

	return 0;
}

/*
 * cmp_hrtime()
 * 
 * Compare two times with us resolution. Assumes times are normalized.
 *
 * Return: -1 - the first time is older
 *          0 - times are the same
 *          1 - the second time is older
 *
 */
int
cmp_hrtime(struct timeval *hrt1, struct timeval *hrt2)
{
	if (hrt1->tv_sec < hrt2->tv_sec)
		return -1;

	if (hrt1->tv_sec > hrt2->tv_sec)
		return 1;

	if (hrt1->tv_usec < hrt2->tv_usec)
		return -1;

	if (hrt1->tv_usec > hrt2->tv_usec)
		return 1;

	/* Times are the same. */
	return 0;
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

/*
 * write()
 * Write a character to a file.
 *
 * `libc' subroutines will use this system routine for output to all files, 
 *  including stdout---so if you need to generate any output, for example to a serial port for
 *  debugging, you should make your minimal write capable of doing this. 
 *
 *  The following minimal implementation is an incomplete example; it relies on a writechar
 *  subroutine (not shown; typically, you must write this in assembler from examples provided
 *   by your hardware manufacturer) to actually perform the output.
 */
int
write(int file, char *ptr, int len)
{
	int todo;
      
	for (todo = 0; todo < len; todo++) {
/*		writechar(*ptr++); */
	}
	return len;
}

