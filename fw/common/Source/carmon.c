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
#include "master_comm.h"
#include "obd.h"
#include "rt_light.h"
#include "utils.h"

#include <errno.h>

/* constants */
/*
 * Activates periodic acquisition of PID values.
 *  - sample rate:  250ms
 *  - fast group:   0x78 (EGT probes), 0xDD (DPF accumulation ratio)
 *  - medium group: 0x05 (Engine coolant temperature)
 *  - slow group:   empty
 */
#define	CM_AUTOSCAN_SR		"00FA"
#define	CM_AUTOSCAN_PIDG	"02010078DD05"

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

	/*
	 * Prepare the device for working in autonomous mode.
	 * Switch to 'assisted mode' in case initialization fails.
	 */
	case CM_AUTO_ST_INIT:
		/* Configure PID autoscan mode. */
		if ((obd_autoscan_conf_sample_rate(CM_AUTOSCAN_SR) == 0) &&
		    (obd_autoscan_conf_pid_groups(CM_AUTOSCAN_PIDG,
		    sizeof (CM_AUTOSCAN_PIDG) - 1) == 0) &&
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

		break;

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
	cm_auto_st = CM_AUTO_ST_INIT;
	cm_assist_st = CM_ASSIST_ST_INIT;
	cm_next_mode_send_ack = FALSE;
	cm_mode = cm_next_mode = CM_MODE_AUTONOMOUS;

	/* Set id of module in response structure. */
	mcom_response_init(&cm_resp_ack_nak, MCOMM_CM_ID);

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
