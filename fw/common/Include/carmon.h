/*********************************************************************
 *
 * Definitions for CarMon service.
 *
 *********************************************************************/

#ifndef __CARMON_H
#define __CARMON_H

#include "GenericTypeDefs.h"

/* Carmon module commands */
#define	CM_CMD_CONF		'C'	/* configure the module */

/* Car states */
#define CM_ENGINE_RUNNING	0x01	/* engine running */
#define CM_ENGINE_WARMED	0x02	/* coolant warmed */
#define CM_VEHICLE_MOVING	0x04	/* vehicle is moving */
#define CM_DPF_REJUV_INPROGRESS	0x08	/* DPF rejuvenation in progress */
#define CM_DISPLAY_DPF_DIST	0x10	/* display DPF distance since rejuv. */

/* types */
/* Operating modes. */
typedef enum _cm_mode_t {
	CM_MODE_CONFIGURE,
	CM_MODE_AUTONOMOUS,
	CM_MODE_ASSISTED
} cm_mode_t;

/* public functions */
/* Initialization routine. */
extern int cm_init(void);

/* Returns current operating mode. */
extern cm_mode_t cm_get_mode(void);

/* Processes query from master. */
extern int cm_process_query(char *, int);

/* Processes PID values received from ECU. */
extern void cm_obd_pid_callback(unsigned char *, int);

/* Processes NMEA sentences received from GPS. */
extern void cm_gps_callback(char *);

#endif /* __CARMON_H */
