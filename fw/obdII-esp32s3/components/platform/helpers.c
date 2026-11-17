#include <errno.h>
#include <stdint.h>
#include "helpers.h"

/* ESP-IDF owns wall-clock time. Never replace time/gettimeofday/settimeofday
 * or advance it by counting RTL interrupts. */
int updatetimeofday(time_t us) { (void)us; return 0; }
int get_curr_hrtime(struct timeval *tv)
{
    if (!tv) return EINVAL;
    return gettimeofday(tv, NULL) == 0 ? 0 : errno;
}
int get_diff_hrtime(struct timeval *from, struct timeval *diff)
{
    if (!from || !diff) return EINVAL;
    struct timeval now;
    int err = get_curr_hrtime(&now);
    if (err) return err;
    int64_t us = ((int64_t)now.tv_sec - from->tv_sec) * US_PER_SECOND + now.tv_usec - from->tv_usec;
    if (us < 0) return EINVAL;
    diff->tv_sec = us / US_PER_SECOND;
    diff->tv_usec = us % US_PER_SECOND;
    return 0;
}
int cmp_hrtime(struct timeval *a, struct timeval *b)
{
    if (a->tv_sec != b->tv_sec) return a->tv_sec < b->tv_sec ? -1 : 1;
    return (a->tv_usec > b->tv_usec) - (a->tv_usec < b->tv_usec);
}
