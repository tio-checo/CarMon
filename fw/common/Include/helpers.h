/*********************************************************************
 *
 * Helper functions
 *
 *********************************************************************/

#ifndef __HELPERS_H
#define __HELPERS_H

#include <sys/time.h>
#include <time.h>

/* constants */
#define US_PER_SECOND	1000000 /* # of microseconds per second */

/* functions */
/* Return current time with us resolution. */
extern int get_curr_hrtime(struct timeval *);

/* Return difference between current time and given time with us resolution. */
extern int get_diff_hrtime(struct timeval *, struct timeval *);

/* Compare two times with us resolution. */
extern int cmp_hrtime(struct timeval *, struct timeval *);

/* updatetimeofday() */
extern int updatetimeofday(time_t);

#endif /* __HELPERS_H */

