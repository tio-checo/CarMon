/*********************************************************************
 *
 * CAN bus analyzer.
 *
 *********************************************************************/

#ifndef __CAN_SNOOP_H
#define __CAN_SNOOP_H

#include "can_hal.h"

/* constants */

/* types */

/* public functions */
/*
 * Initializes analyzer.
 */
extern void can_snoop_init(void);

/*
 * Processes master query.
 *
 * Returns:
 *       0 - snooping mode successfully configured
 *  EINVAL - invalid data provided
 *  EBUSY  - analysis already in progress
 */
extern int can_snoop_process_query(char *, int);

/* Lets received frame undergo process of analysis. */
extern int can_snoop_process_frame(can_frame_t *);

/* Returns number of dropped frames. */
extern int can_snoop_get_num_dropped_frames(void);

/*
 * Snoop callback function. Called periodically from the application task.
 * Handles snooping timers.
 */
extern void can_snoop_cb(void);

#endif /* __CAN_SNOOP_H */

