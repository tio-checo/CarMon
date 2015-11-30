/*********************************************************************
 *
 * Definitions for GPS service
 *
 * GPS service communicates with L80 Quectel GPS module via Bluetooth.
 * Receives data in NMEA0183 format and logs them to the log device.
 *
 *********************************************************************/

#ifndef __GPS_H
#define __GPS_H

/* Initialization routine. */
extern int gps_init(void);
/* Returns status of GPS device. */
extern int gps_get_status(void);
/* Prints diagnostic information. */
extern void gps_print_diag_info(int (*)(const char *));
#endif /* __GPS_H */
