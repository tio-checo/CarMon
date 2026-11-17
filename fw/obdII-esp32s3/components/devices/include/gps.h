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

/*
 * UART definitions.
 * Used if GPS module communicates over KWP2000 K-line.
 */
#ifndef KWP2K_MODULE
#define GPS_RX_PIN	2	/* RD2 */
#define GPS_RX_MASK	(1 << GPS_RX_PIN)
#define GPS_TX_PIN	3	/* RD3 */
#define GPS_TX_MASK	(1 << GPS_TX_PIN)

/* Default GPS baudrate */
#define	GPS_BAUDRATE	9600
#endif

/* Initialization routine. */
extern int gps_init(void);
/* Returns status of GPS device. */
extern int gps_get_status(void);
/* Returns current course - obtained from RMC sentence. */
extern char gps_get_course(void);
/* Prints diagnostic information. */
extern void gps_print_diag_info(int (*)(const char *));
/* Suspends the module. */
extern int gps_suspend(void);
#endif /* __GPS_H */
