/*********************************************************************
 *
 * Implements CarMon service.
 *
 * CarMon device can operate in following modes:
 *
 * o Autonomous
 *   Default mode entered after the device is powered up. The device operates
 *   according to previously configured instructions.
 *
 * o Configuration
 *
 * o Assisted
 *   The device is controlled from remote terminal.
 *
 *********************************************************************/

#include "HardwareProfile.h"
#include "GenericTypeDefs.h"
#include "xc.h"

#include "carmon.h"
#include "dlfs.h"
#include "ee24.h"
#include "master_comm.h"
#include "obd.h"
#include "pic_core.h"
#include "rt_light.h"
#include "utils.h"

#include <errno.h>

/* constants */
/*
 * Defaults values for periodic acquisition mode.
 * Used in case EEPROM parameters are invalid.
 */
/*
 * Activates periodic acquisition of PID values.
 *  - sample rate:  20ms
 *  - Fast group
 *    0C - RPM
 *    10 - MAF
 *    23 - Rail pressure
 *    EF - DPF differential pressure
 *
 *  - Medium group
 *    04 - Calculated load
 *    0B - Intake manifold absolute pressure
 *    0D - Vehicle speed
 *    2C - Commanded EGR
 *    49 - Accelerator pedal position D
 *    
 *  - Slow group
 *    05 - Engine coolant temperature
 *    0F - Intake air temperature
 *    33 - Barometric pressure
 *    78 - EGT bank 1
 *    AD - Injector Feedback Values
 *    DD - DPF accumulation ratio
 */
#define	CM_AUTOSCAN_SR		"0014"
#define	CM_AUTOSCAN_PIDG	"0405060C1023EF040B0D2C49050F3378ADDD"

/* "sample rate" EEPROM parameter number */
#define CM_EEPAR_SR		1
/* "PID groups" EEPROM parameter number */
#define CM_EEPAR_PIDG		2

/* types */
/* States in 'Configuration' mode */
typedef enum _cm_conf_state_t {
	CM_CONF_ST_IDLE,
	CM_CONF_ST_INIT,
	CM_CONF_ST_RUNNING
} cm_conf_state_t;

/* States in 'Autonomous' mode */
typedef enum _cm_auto_state_t {
	CM_AUTO_ST_IDLE,
	CM_AUTO_ST_WAIT4CORESVC,
	CM_AUTO_ST_INIT,
	CM_AUTO_ST_RUNNING,
	CM_AUTO_ST_WAIT4IDLE
} cm_auto_state_t;

/* States in 'Assisted' mode */
typedef enum _cm_assisted_state_t {
	CM_ASSIST_ST_IDLE,
	CM_ASSIST_ST_INIT,
	CM_ASSIST_ST_RUNNING
} cm_assist_state_t;

/* local variables */
/* structure holding ACK/NAK response to master */
static mcom_response_t cm_resp_ack_nak;
/* current mode of operation */
static cm_mode_t cm_mode;
/* next mode of operation */
static cm_mode_t cm_next_mode;
/*
 * If set to TRUE, ACK/NAK is sent to master after transition
 * to another mode is finished.
 */
static BOOL cm_next_mode_send_ack;
/* current state when operating in configuration mode */
static cm_conf_state_t	cm_conf_st;
/* current state when operating in autonomous mode */
static cm_auto_state_t	cm_auto_st;
/* current state when operating in assisted mode */
static cm_assist_state_t	cm_assist_st;

/*
 * Log record - buffer needs to be big enough to be able to hold
 * all types of log records.
 */
static char log_record[150];

/* TRUE if log device available; FALSE otherwise. */
static BOOL cm_log_available;
/* Set to TRUE once the first ECU PID is logged to the log device. */
static BOOL cm_ecu_pid_logged;

/* local functions */

/*
 * Initiates transition to another operating mode.
 */
static int
cm_request_mode_change(cm_mode_t next_mode, BOOL send_ack)
{
	cm_next_mode = next_mode;
	cm_next_mode_send_ack = send_ack;

	return 0;
}

/*
 * Finishes transition to another operating mode.
 */
static void
cm_finish_mode_change(void)
{
	cm_mode = cm_next_mode;
}

/*
 * Returns TRUE if mode change is requested. FALSE otherwise.
 */
static BOOL
cm_mode_change_requested(void)
{
	return (cm_next_mode != cm_mode);
}

/*
 * Sends ACK to master.
 */
static int
cm_send_ack_response_to_master(void)
{
	int err;

	err = mcom_prepare_ack_response(&cm_resp_ack_nak);

	if (err == 0) {
		mcom_queue_response(&cm_resp_ack_nak);
	}

	return err;
}

/*
 * Sends NAK to master.
 */
static int
cm_send_nak_response_to_master(unsigned char e)
{
	int err;

	err = mcom_prepare_nak_response(&cm_resp_ack_nak, e);

	if (err == 0) {
		mcom_queue_response(&cm_resp_ack_nak);
	}

	return err;
}

/*
 * If requested, inform master about result
 * of transition to different operating mode.
 */
static void
cm_send_change_mode_result_to_master(int res)
{
	/* Do not inform the master. */ 
	if (!cm_next_mode_send_ack) {
		return;
	}

	cm_next_mode_send_ack = FALSE;

	if (res == 0) {
		cm_send_ack_response_to_master();
	} else {
		cm_send_nak_response_to_master(res);
	}
}

/*
 * Service routine handling 'Configuration mode'
 */
static int
cm_config_svc(void)
{
	switch (cm_conf_st) {
	/* Idle mode. */
	case CM_CONF_ST_IDLE:
		break;

	/* Prepare the device for working in 'configure' mode. */
	case CM_CONF_ST_INIT:
		/* Nothing to be done for now. */

		/* Send result of the operation to the master. */
		cm_send_change_mode_result_to_master(0);

		cm_conf_st = CM_CONF_ST_RUNNING;
		break;

	/*
	 * Now running in 'configure' mode...
	 * Run until mode transition is requested.
	 */
	case CM_CONF_ST_RUNNING:
		if (!cm_mode_change_requested()) {
			break;
		}

		/*
		 * Make sure we start with initialization phase
		 * next time assisted mode is entered.
		 */
		cm_conf_st = CM_CONF_ST_INIT;

		/* Finish transition. */
		cm_finish_mode_change();
		break;
	} /* switch */

	return 0;
}

/*
 * Service routine handling 'Autonomous mode'
 */
static int
cm_autonomous_svc(void)
{
	int err;

	switch (cm_auto_st) {
	/* Idle mode. */
	case CM_AUTO_ST_IDLE:
		break;

	/* Wait until core services are up and running */
	case CM_AUTO_ST_WAIT4CORESVC:
		if (!pcore_svc_online()) {
			break;
		}

		cm_auto_st = CM_AUTO_ST_INIT;

		break;

	/*
	 * Prepare the device for working in autonomous mode.
	 * Switch to 'assisted mode' in case initialization fails.
	 */
	case CM_AUTO_ST_INIT: {
		int sr_ln, pidg_ln;
		unsigned char *sr_pv, *pidg_pv;

		/*
		 * Configure PID autoscan mode from EEPROM parameters.
		 * Fallback to default values in case EEPROM content is invalid.
		 */
		if ((ee24_getpar(CM_EEPAR_SR, &sr_ln, &sr_pv) == 0) &&
		    (ee24_getpar(CM_EEPAR_PIDG, &pidg_ln, &pidg_pv) == 0) &&
		    (obd_autoscan_conf_sr(sr_pv, FALSE) == 0) &&
		    (obd_autoscan_conf_pidg(pidg_pv, pidg_ln, FALSE) == 0) &&
		    (obd_autoscan_conf_mode(TRUE) == 0)) {
			/* Autonomous mode successfully initialized. */
			err = 0;
			cm_auto_st = CM_AUTO_ST_RUNNING;
		} else if ((obd_autoscan_conf_sr(CM_AUTOSCAN_SR, TRUE) == 0) &&
		    (obd_autoscan_conf_pidg(CM_AUTOSCAN_PIDG,
		    sizeof (CM_AUTOSCAN_PIDG) - 1, TRUE) == 0) &&
		    (obd_autoscan_conf_mode(TRUE) == 0)) {

			/* Autonomous mode successfully initialized. */
			err = 0;
			cm_auto_st = CM_AUTO_ST_RUNNING;
		} else {
			/* Initialization failed. Fall back to assisted mode. */
			err = EINVAL;
			cm_mode = cm_next_mode = CM_MODE_ASSISTED;
		}

		/* Send result of the operation to the master. */
		cm_send_change_mode_result_to_master(err);

	} break;

	/*
	 * Autonomous mode running...
	 * Run until mode transition is requested.
	 */
	case CM_AUTO_ST_RUNNING:
		/*
		 * If mode is to be changed,
		 * instruct obd module to enter idle mode.
		 */
		if (cm_mode_change_requested()) {
			cm_auto_st = CM_AUTO_ST_WAIT4IDLE;
		}

		break;

	/* Wait until obd module enters idle mode. */
	case CM_AUTO_ST_WAIT4IDLE:
		if (obd_goto_idle() == EAGAIN) {
			break;
		}

		/*
		 * Make sure we start with initialization phase
		 * next time autonomous mode is entered.
		 */
		cm_auto_st = CM_AUTO_ST_INIT;

		/* Finish transition. */
		cm_finish_mode_change();
		break;
	}

	return 0;
}

/*
 * Service routine handling 'Assisted mode'
 */
static int
cm_assisted_svc(void)
{
	switch (cm_assist_st) {
	/* Idle mode. */
	case CM_ASSIST_ST_IDLE:
		break;

	/* Prepare the device for working in assisted mode. */
	case CM_ASSIST_ST_INIT:
		/* Nothing to be done for now. */

		/* Send result of the operation to the master. */
		cm_send_change_mode_result_to_master(0);

		cm_assist_st = CM_ASSIST_ST_RUNNING;
		break;

	/*
	 * Assisted mode running...
	 * Run until mode transition is requested.
	 */
	case CM_ASSIST_ST_RUNNING:
		if (!cm_mode_change_requested()) {
			break;
		}

		/*
		 * Make sure we start with initialization phase
		 * next time assisted mode is entered.
		 */
		cm_assist_st = CM_ASSIST_ST_INIT;

		/* Finish transition. */
		cm_finish_mode_change();
		break;
	} /* switch */

	return 0;
}

/*
 * Main service routine.
 */
static int
cm_main_svc(void)
{
	/*
	 * Invoke appropriate service routine.
	 */
	switch (cm_mode) {
	case CM_MODE_CONFIGURE:
		cm_config_svc();
		break;

	case CM_MODE_AUTONOMOUS:
		cm_autonomous_svc();
		break;

	case CM_MODE_ASSISTED:
		cm_assisted_svc();
		break;
	}

	return 0;
}

/*
 * Configures module.
 * Returns:
 *        0 - configuration process successfully finished
 *   EAGAIN - configuration process successfully commenced
 *   EINVAL - input data invalid
 */
static int
cm_conf(char *data, int ln)
{
	int mode;

	if (ln != 2) {
		return EINVAL;
	}

	/* Operating mode. */
	mode = hex2ulong(data, 2);

	if ((mode < CM_MODE_CONFIGURE) || (mode > CM_MODE_ASSISTED)) {
		return EINVAL;
	}

	/* No change. */
	if (cm_mode == mode) {
		return 0;
	}

	/* Initiate transition, send result of the operation to the master. */
	cm_request_mode_change(mode, TRUE);

	return EAGAIN;
}

/* Logs PID value to log device. */
static void
cm_log_pid(unsigned char *d, int ln)
{
	char *ts = pcore_gettime();

	/*
	 * Save PID value to log device.
	 * Log record is in CSV format as specified in rfc4180:
	 *
	 * <timestamp>,ECU<mode><pid number>,<pid value>CRLF
	 *
	 * format of <timestamp>: YYYYMMDDhhmmssmmm
	 * <pid value> is in form of raw data received from ECU
	 *
	 */

	/*
	 * Log timestamp and PID address first. Resulting string
	 * fits the available buffer for sure, so no need to
	 * check for buffer overflow.
	 */
	log_record[0] = '\0';
	strcat(log_record, ts);
	strcat(log_record + 17, ",ECU01");
	uint2hex(d[0], log_record + 23, 1);
	log_record[25] = ',';

	/*
	 * Append PID value.
	 * Handle scenario when resulting log record does not fit
	 * a buffer. Log error string instead of PID value in such case.
	 */
	if (26 + 2 * (ln - 1) + 3 > sizeof (log_record)) {
		log_record[26] = '\0';
		strcat(log_record + 26, "ERR_OVERFLOW\r\n");
	} else {
		int i;
		size_t pos;

		for (i = 1, pos = 26; i < ln; i++, pos += 2) {
			uint2hex(d[i], log_record + pos, 1);
		}

		/* Append 'end of log record' marker. */
		log_record[pos++] = '\r';
		log_record[pos++] = '\n';
		log_record[pos] = '\0';
	}

	/*
	 * Write log record to log device.
	 * dlfs_write() is non-blocking function, so can be called
	 * from interrupt context.
	 */
	dlfs_write(0, log_record, strlen(log_record));
}

/* Logs GPS data to log device. */
static void
cm_log_gps(char *d)
{
	char *ts = pcore_gettime();

	/*
	 * Log record is in CSV format as specified in rfc4180:
	 *
	 * <timestamp>,GPS,<NMEA sentence>CRLF
	 *
	 * format of <timestamp>: YYYYMMDDhhmmssmmm
	 *
	 */

	/*
	 * timestamp and name-value pair are to be logged.
	 * strcat guarantees that the resulting string is NULL-terminated.
	 */
	log_record[0] = '\0';
	strcat(log_record, ts);
	strcat(log_record + 17, ",GPS,");
	strcat(log_record + 22, d);
	strcat(log_record, "\r\n");

	/*
	 * Write log record to log device.
	 * dlfs_write() is non-blocking function, so can be called
	 * from interrupt context.
	 */
	dlfs_write(0, log_record, strlen(log_record));
}

/* public functions */

/*
 * Processes query from master.
 */
int
cm_process_query(char *data, int ln)
{
	int err = 0;
	BOOL translate_err = TRUE;
	BOOL respond_with_ack = TRUE;
	BOOL respond_now = TRUE;

	/* Handle OBD commands */
	switch (data[0]) {
	/* Configure the module. */
	case CM_CMD_CONF:
		err = cm_conf(&data[1], ln - 1);

		if (err == EAGAIN) {
			/*
			 * Let OBD service respond later once
			 * configuration process is finished.
			 */
			respond_now = FALSE;
		} else if (err != 0) {
			/* Unrecoverable error. */
			respond_with_ack = FALSE;
		}

		break;

	/* unknown command */
	default:
		err = MCOM_NAK_UNKNOWN_IDCMD;
		translate_err = FALSE;
		respond_with_ack = FALSE;

		break;
	} /* switch */

	/* Do not respond now, Carmon service will take care of that. */
	if (!respond_now)
		return 0;

	/* Send response to master indicating that command was processed. */
	if (respond_with_ack) {
		cm_send_ack_response_to_master();
	} else {
		/* Translate error code if appropriate. */
		if (translate_err)
			err = mcom_err_translate(err);

		cm_send_nak_response_to_master(err);
	}

	return 0;
}

/* Main initialization routine. */
int
cm_init(void)
{
	/* Enter 'autonomous mode' after power up. */
	cm_conf_st = CM_CONF_ST_INIT;
	cm_auto_st = CM_AUTO_ST_WAIT4CORESVC;
	cm_assist_st = CM_ASSIST_ST_INIT;
	cm_next_mode_send_ack = FALSE;
	cm_mode = cm_next_mode = CM_MODE_AUTONOMOUS;

	/* Set id of module in response structure. */
	mcom_response_init(&cm_resp_ack_nak, MCOMM_CM_ID);

	/* Create new log file. */
	cm_log_available = dlfs_open("obd_log", O_CREAT) == 0;

	/* No ECU PID logged yet. */
	cm_ecu_pid_logged = FALSE;

	/* Clear IO statistics. */
	dlfs_kstat_clear();

	/* Register and enable main service routine. */
	rtl_svc_init("CarMon", cm_main_svc, TRUE, TRUE);

	return 0;
}

/* Returns current operating mode. */
cm_mode_t
cm_get_mode(void)
{
	return cm_mode;
}

/* Processes PID values received from ECU. */
void
cm_obd_pid_callback(unsigned char *d, int ln)
{
	/* Log PID to log device if available. */
	if (cm_log_available) {
		cm_log_pid(d, ln);

		cm_ecu_pid_logged = TRUE;
	}
}

/*
 * Processes NMEA sentences received from GPS.
 * It is expected that they are provided in form of NULL-terminated strings.
 */
void
cm_gps_callback(char *d)
{
	/*
	 * Start loggin NMEA sentence to the log device
	 * only after at least one ECU PID value has been logged.
	 * That assures that log inode is not wasted in case
	 * connection to ECU is not established before the logger
	 * is turned off.
	 */
	if (cm_ecu_pid_logged) {
		cm_log_gps(d);
	}
}
