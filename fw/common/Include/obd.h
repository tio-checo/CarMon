/*********************************************************************
 *
 * Handles OBD communication layer with ECU. This layer is independent
 * on real communication protocol used (KWP2000, CAN ...).
 *
 *********************************************************************/

#ifndef __OBD_H
#define __OBD_H

#include "GenericTypeDefs.h"
#include "CAN.h"

/* ECU submodule IDs */
#define	ECU_OBD_ID		'O'	/* OBD module */

/* OBD module commands */
#define	OBD_CMD_CONF		'C'	/* configure the module */
#define	OBD_CMD_GETFFPID	'F'	/* get value of freeze frame PID */
#define	OBD_CMD_GETCURPID	'P'	/* get value of current PID */
#define	OBD_CMD_GETCURPIDS	'p'	/* get values of current PIDs periodically */
#define	OBD_CMD_GETPERMDTC	'D'	/* get permanent/confirmed DTC */
#define	OBD_CMD_RESETDTC	'R'	/* reset/clear DTCs, freeze frames... */

/* OBD modes */
#define	OBD_MODE_GET_PID	1	/* Show current data */
#define	OBD_MODE_GET_FF		2	/* Show freeze frame data */
#define	OBD_MODE_GET_DTC	3	/* Show Diagnostic Trouble Codes */
#define	OBD_MODE_RESET_DTC	4	/* Clear Diagnostic Trouble Codes */
#define	OBD_MODE_GET_PID_TOYOTA	0x21	/* Get value of Toyota specific PID */

#define	OBD_ECU_PACKET_MAXLN	256	/* Max length of ECU packet */

/* OBD PIDs - used by alarms */
/*
 * Exhaust Gas temperature (EGT) Bank 1
 * A request for this PID will return 9 bytes of data.
 * The first byte is a bit encoded field indicating
 * which EGT sensors are supported:
 *
 * byte     description
 * ------------------------
 *  A    supported EGT sensors
 * B-C   temperature EGT11
 * D-E   temperature EGT12
 * F-G   temperature EGT13
 * H-I   temperature EGT14
 *
 * The first byte is bit-encoded as follows:
 *
 *  bit      description
 * ------------------------
 * A7-A4     reserved
 *  A3    EGT14 sensor supported
 *  A2    EGT13 sensor supported
 *  A1    EGT12 sensor supported
 *  A0    EGT11 sensor supported
 *
 * The remaining bytes are 16 bit integers indicating the temperature
 * in degrees Celsius in the range -40 to 6513.5 (scale 0.1),
 * using the usual (A * 256 + B) / 10 - 40 formula (MSB is A, LSB is B).
 * Only values for which the corresponding sensor is supported are meaningful.
 */
#define	OBD_PID_EGT1		0x78
/*
 * Given raw packet received from ECU, the macro extracts value
 * for 'n'th EGT sensor and converts the raw value to Celsius degrees.
 * For simplicity, macro does not check if particular sensor is supported.
 */
#define OBD_GET_EGT_VALUE(er, n)	\
    ((256 * ((er)[2 * (n) - 2]) + ((er)[2 * (n) - 1])) / 10 - 40)

/* max number of PIDs scanned in auto-scan mode */
#define	OBD_PID_LIST_LN		50
/* number of PID groups */
#define	OBD_PID_GROUP_NUM	3

/*
 * Determines mode (standard OBD, Toyota) from PID number.
 * PIDs above 0x80 are reserved for Toyota
 */
#define	OBD_GET_MODE_FROM_PID(pn)	\
    ((pn) < 0x80 ? OBD_MODE_GET_PID : OBD_MODE_GET_PID_TOYOTA)

/* Underlying communication layer. */
typedef enum {
    OBD_COMM_MODE_AUTO = 0,		/* Determined automatically */
    OBD_COMM_MODE_KWP2K_NORM,		/* KWP 2000 - normal mode */
    OBD_COMM_MODE_KWP2K_DEBUG,		/* KWP 2000 - debug mode */
    OBD_COMM_MODE_KWP2K_MIXED,		/* KWP 2000 mixed with CAN */
    OBD_COMM_MODE_CAN_SID_250,		/* CAN - 11bit ID, 250kbit */
    OBD_COMM_MODE_CAN_SID_500,		/* CAN - 11bit ID, 500kbit */
    OBD_COMM_MODE_CAN_EID_250,		/* CAN - 29bit ID, 250kbit */
    OBD_COMM_MODE_CAN_EID_500,		/* CAN - 29bit ID, 500kbit */
    OBD_COMM_MODE_NUMBER_OF_MODES
} OBD_COMM_MODE;

/*
 * Definitions related to concept of PID groups.
 *
 * Concept of PID groups was originaly introduced in order to address
 * limited data bandwith between ECU and tester. When sending data
 * over K-line via KWP2000 protocol, it is possible to obtain data
 * with refresh rate approximately 100ms. When more PID values are being
 * acquired at the same time, overall refresh time decreases significantly.
 * To mitigate that problem, PIDs are divided into several groups
 * taking into account their real time characteristics.
 * In general, PIDs which change frequently (RPM, rail pressure, ...) are
 * sampled more frequently than PIDs which change only slowly over the time
 * (water coolant temperature, atmospheric pressure, ...).
 * This concept is applicable even if using CAN as a transport media,
 * as it lowers total amount of transferred data.
 *
 * Currently, three groups are defined: 'fast', 'medium', 'slow',
 * 'fast' having higher priority than 'medium' and 'medium' having higher
 * priority than 'slow'.
 * As a rule of thumb, all PIDs in higher priority group are processed
 * first, then one PID from group with lower priority is processed.
 * There are two attributes associated with each group:
 *   curr - relative index, points to currently processed PID in a group
 *   num - number of PIDs in a group
 *
 * PIDs in all groups are stored in linear list from 'fast' to 'slow' group.
 *
 * |          fast         ||         medium        ||      slow     |
 * -------------------------------------------------------------------
 * | PIDf1 | PIDf2 | PIDf3 || PIDm1 | PIDm2 | PIDm3 || PIDs1 | PIDs2 |
 * -------------------------------------------------------------------
 *
 * In the example above, PIDs would be scanned in following order:
 * f1f2f3m1f1f2f3m2f1f2f3m3f1f2f3s1f1f2f3m1f1f2f3m2f1f2f3m3f1f2f3s2
 */
typedef struct _obd_pid_group_t {
	/* Number of PIDs in a group. */
	int	num;
	/*
	 * Index of currently processed PID - relative
	 * to the beginning of the group.
	 */
	int	curr;
} obd_pid_group_t;

/* Autoscan PID groups */
typedef struct _obd_pid_groups_t {
	/* Index of currently processed PID within PID list. */
	int		curr;
	/* Total number of PIDs in PID list. */
	int		num;
	/* linear list of PIDs */
	unsigned char	list[OBD_PID_LIST_LN];
	/* group state */
	obd_pid_group_t	group[OBD_PID_GROUP_NUM];
} obd_pid_groups_t;

/* Initialization routine */
extern int obd_init(void);

/* Processes query from master targeted to ECU module */
extern int obd_process_query(char *, int);

/* Callback routine processing received CAN frames */
extern int obd_can_rx_cb(CANRxMessageBuffer *);

/* Activates or deactivates PID autoscan mode. */
extern int obd_autoscan_conf_mode(BOOL);

/* Configures PID groups for autoscan mode. */
extern int obd_autoscan_conf_pid_groups(char *, int);

/* Configures sample rate for PID autoscan mode. */
extern int obd_autoscan_conf_sample_rate(char *);

/* Instructs OBD module to enter idle mode. */
extern int obd_goto_idle(void);

/* Returns TRUE if OBD module is currently IDLE. FALSE otherwise. */
extern BOOL obd_is_idle(void);
#endif /* __OBD_H */
