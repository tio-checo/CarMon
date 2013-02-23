/*********************************************************************
 *
 * Module collecting data from various sensors connected directly to
 * PIC module (not via ECU).
 *
 * For now, only battery voltage is measured via 10bit AD converter.
 *
 *********************************************************************/

#include <errno.h>

#include "adc10.h"

#include "data_acq.h"
#include "master_comm.h"
#include "rt_light.h"
#include "utils.h"

/* local constants */
#define ADC_NUM_CHANNELS	10	/* total number of analog channels */
#define ADC_CH_VBAT		8	/* battery voltage measured on AN8 */

/* states of state machine collecting data from A/D converter */
#define ADC_ST_IDLE		0	/* state machine idle */
#define ADC_ST_SAMPLE		1	/* sampling phase */
#define ADC_ST_CONVERT		2	/* conversion phase */

/* states of oscilloscope state machine */
#define	DAQ_SCOPE_ST_IDLE	0	/* state machine idle */
#define	DAQ_SCOPE_ST_PRETRIG	1	/* pre-trigger phase */
#define	DAQ_SCOPE_ST_WAIT4TRIG	2	/* 'waiting for trigger' phase */
#define	DAQ_SCOPE_ST_POSTTRIG	3	/* post-trigger phase */
#define	DAQ_SCOPE_ST_SEND_DATA	4	/* send acquired data to master */

/* states of trigger state machine */
#define	DAQ_TRIG_ST_REDGE_P1	1	/* rising edge - phase 1 */
#define	DAQ_TRIG_ST_REDGE_P2	2	/* rising edge - phase 2 */
#define	DAQ_TRIG_ST_FEDGE_P1	3	/* falling edge - phase 1 */
#define	DAQ_TRIG_ST_FEDGE_P2	4	/* falling edge - phase 2 */

/* local variables */
static unsigned int adc_data[ADC_NUM_CHANNELS];	/* measured ADC values */
static int daq_adc_st;				/* ADC state machine */
static int daq_adc_ch;				/* measured channel */
static BOOL daq_adc_sample_acquired;		/* new sample available */
static int daq_adc_sample_rate;			/* sample rate in ticks */

/* oscilloscope module */
static daq_main_t	daq_main;	/* main */
static daq_trigger_t	daq_trigger;	/* trigger */
static daq_time_base_t	daq_time_base;	/* time base */
static daq_buffer_t	daq_buffer;	/* acquisition buffer */

/* structure holding ACK/NAK response to master */
static mcom_response_t daq_resp_ack_nak;
/* If set to TRUE, send ack/nak response packet to master */
static BOOL fl_send_ack_nak;

/* structure holding data response to master */
static mcom_response_t daq_resp_data;
/*
 * Buffer for data payload to be sent to master. It has the same size
 * as data acquisition buffer
 */
static unsigned int daq_resp_buf[DAQ_BUF_SIZE];
/* If set to TRUE, send response containing data to master */
static BOOL fl_send_data;

/* Timers */
/*
 * Measures delay between two acquisition cycles to achieve desired
 * refresh period.
 */
static rtl_timer_t *daq_refresh_timer;

/* local functions */
/*
 * Service routine collecting data from sensors connected to PIC
 * 10bit AD converter with configurable sample rate.
 * For now only measures battery voltage - connected to analog channel 8.
 */
static int
daq_adc_svc(void)
{
	/* local timer serving for measuring sample rate */
	static int daq_sr_timer = 1;

	switch (daq_adc_st) {
	/* machine idle */
	case ADC_ST_IDLE:
		break;

	/* sampling phase */
	case ADC_ST_SAMPLE:
		/* decrement sample rate timer */
		if (daq_sr_timer > 0) {
			daq_sr_timer--;
		}

		/* Timer expired, arm it again and initiate conversion phase */
		if (daq_sr_timer == 0) {
			daq_sr_timer = daq_adc_sample_rate;
			AD1CON1CLR = ADC_SAMP_ON;

			/* move to conversion phase */
			daq_adc_st++;
		}

		break;

	/* conversion phase */
	case ADC_ST_CONVERT:
		/* decrement sample rate timer while waiting for the result */
		if (daq_sr_timer > 0) {
			daq_sr_timer--;
		}

		/* wait until conversion is done */
		if (!(AD1CON1 & ADC_CONVERSION_DONE))
			break;

		/* clear 'conversion done' flag */
		AD1CON1CLR = ADC_CONVERSION_DONE;

		/* store measured value */
		adc_data[daq_adc_ch] = ADC1BUF0;

		/* let upper module know, new sample has been acquired */
		daq_adc_sample_acquired = TRUE;

		/* select next channel - only Vbat channel used for now */
		AD1CHS = daq_adc_ch << 16;

		/* Initiate AD sampling phase. */
		AD1CON1SET = ADC_SAMP_ON;

		/* move to sampling phase */
		daq_adc_st--;

		break;
	} /* switch */

	return 0;
}

/*
 * Returns particular sample from acquisition buffer
 *  index < 0 - samples older than current 
 *  index = 0 - current sample
 */
static unsigned int
daq_get_sample(int index)
{
	int i;

	i = daq_buffer.tail - 1 + index;

	if (i < 0) {
		i += DAQ_BUF_SIZE;
	} else if (i > DAQ_BUF_SIZE) {
		i -= DAQ_BUF_SIZE;
	}

	return daq_buffer.buf[i];
}

/*
 * Initializes trigger unit
 */
static int
daq_trig_init(void)
{
	/* Pick up trigger state machine based on selected trigger event */
	switch (daq_trigger.event) {
	/* rising edge */
	case DAQ_TRIG_REDGE:
		daq_main.trigger_st = DAQ_TRIG_ST_REDGE_P1;
		break;

	/* falling edge */
	case DAQ_TRIG_FEDGE:
		daq_main.trigger_st = DAQ_TRIG_ST_FEDGE_P1;
		break;

	}

	return 0;
}

/*
 * State machine detecting trigger event.
 * Returns TRUE if trigger event was detected, otherwise returns FALSE.
 */
static BOOL
daq_trig_found(void)
{
	switch (daq_main.trigger_st) {
	/*
	 * rising edge - phase 1
	 * Wait until at least three subsequent samples are below trigger level.
	 */
	case DAQ_TRIG_ST_REDGE_P1:
		if ((daq_get_sample(0) < daq_trigger.lvl) &&
		    (daq_get_sample(-1) < daq_trigger.lvl) &&
		    (daq_get_sample(-2) < daq_trigger.lvl))
			daq_main.trigger_st = DAQ_TRIG_ST_REDGE_P2;

		break;

	/*
	 * rising edge - phase 2
	 * Wait until for sample above trigger level.
	 */
	case DAQ_TRIG_ST_REDGE_P2:
		if (daq_get_sample(0) > daq_trigger.lvl)
		return TRUE;

		/* NOT REACHED */
		break;

	/*
	 * falling edge - phase 1
	 * Wait until at least three subsequent samples are above trigger level.
	 */
	case DAQ_TRIG_ST_FEDGE_P1:
		if ((daq_get_sample(0) > daq_trigger.lvl) &&
		    (daq_get_sample(-1) > daq_trigger.lvl) &&
		    (daq_get_sample(-2) > daq_trigger.lvl))
			daq_main.trigger_st = DAQ_TRIG_ST_FEDGE_P2;
		break;

	/*
	 * falling edge - phase 2
	 * Wait until for sample below trigger level.
	 */
	case DAQ_TRIG_ST_FEDGE_P2:
		if (daq_get_sample(0) < daq_trigger.lvl)
		return TRUE;

		/* NOT REACHED */
		break;

	}

	return FALSE;
}

/*
 * State machine providing 'oscilloscope' capability. It works in following way.
 *  - acquires pre-trigger number of samples
 *  - continues with data acquisition waiting for trigger condition to occur
 *  - detects the trigger condition
 *  - acquires post-trigger number of samples
 */
static int
daq_scope_svc(void)
{
	/* counts number of post-trigger samples */
	static int pt_num;

	/* If buffer is being filled, wait for new sample acquired. */
	if (daq_buffer.fill && !daq_adc_sample_acquired)
		return 0;

	/* If buffer filling in progress, save the sample. */
	if (daq_buffer.fill) {
		daq_get_adc_value(ADC_CH_VBAT,
		    daq_buffer.buf + daq_buffer.tail);

		/*
		 * Move to the next free position in buffer.
		 * Buffer is cyclic, so when it gets full, start dropping the
		 * oldest samples.
		 */
		if (++daq_buffer.sn < DAQ_BUF_SIZE) {
			daq_buffer.tail++;
		} else {
			daq_buffer.sn = DAQ_BUF_SIZE;

			/* Buffer is full, move both head and tail markers */
			if (++daq_buffer.tail >= DAQ_BUF_SIZE) {
				daq_buffer.tail = 0;
			}
			if (++daq_buffer.head >= DAQ_BUF_SIZE) {
				daq_buffer.head = 0;
			}
		}
	}

	/* clear the flag */
	daq_adc_sample_acquired = FALSE;

	switch (daq_main.scope_st) {
	/* machine idle */
	case DAQ_SCOPE_ST_IDLE:
		/* Acquisition stopped */
		if (!daq_main.run)
			break;

		/* Wait until previously acquired data are processed */
		if (daq_buffer.ready)
			break;

		/* Wait until next acquisition cycle is to be commenced */
		if (!rtl_timer_expired(daq_refresh_timer))
			break;

		/* Arm the timer again */
		rtl_timer_init(&daq_refresh_timer, daq_main.acq_per);

		/* Prepare acquisition buffer */
		daq_buffer.head = daq_buffer.tail = daq_buffer.sn = 0;

		/* Initiate acquisition */
		daq_buffer.fill = TRUE;
		daq_main.scope_st = DAQ_SCOPE_ST_PRETRIG;

		break;

	/* pre-trigger phase */
	case DAQ_SCOPE_ST_PRETRIG:
		/*
		 * Wait until number of required pre-trigger
		 * samples is obtained.
		 */
		if (daq_buffer.sn < daq_time_base.pre)
			break;

		/*
		 * Pre-trigger phase finished, now wait for trigger unless
		 * 'auto' trigger mode is selected in which case skip that
		 * phase.
		 */
		if (daq_trigger.mode == DAQ_TRIG_AUTO) {
			daq_buffer.trig = daq_buffer.tail;
			pt_num = 0;

			daq_main.scope_st = DAQ_SCOPE_ST_POSTTRIG;
		} else {
			/* Initialize trigger unit. */
			daq_trig_init();
			daq_main.scope_st = DAQ_SCOPE_ST_WAIT4TRIG;
		}

		break;

	/* wait for trigger */
	case DAQ_SCOPE_ST_WAIT4TRIG:
		if (daq_trig_found()) {
			pt_num = 0;
			daq_main.scope_st = DAQ_SCOPE_ST_POSTTRIG;
		}

		break;

	/* post-trigger phase */
	case DAQ_SCOPE_ST_POSTTRIG:
		/*
		 * Wait until number of required post-trigger
		 * samples is obtained.
		 */
		if (++pt_num < daq_time_base.post) {
			break;
		}

		/*
		 * Acquisition cycle finished. Stop filling the buffer.
		 * Signal upper layer we are done. If operating in continuous
		 * acquisition mode, wait until next acquisition cycle is
		 * to be commenced.
		 */
		daq_buffer.fill = FALSE;
		daq_buffer.ready = TRUE;

		/*
		 * If operating in 'single' acquisition mode,
		 * stop acquisition
		 */
		if (daq_time_base.mode == DAQ_ACQ_MODE_SINGLE)
			daq_main.run = FALSE;

		daq_main.scope_st = DAQ_SCOPE_ST_IDLE;

		break;

	} /* switch */

	return 0;
}

/*
 * Post processing service. Processes acquired data before they are sent
 * to master.
 */
static int
daq_post_process_svc()
{
	int num;

	/* There are no data to be processed, return */
	if (!daq_buffer.ready)
		return 0;

	/*
	 * Send raw data to master
	 * wait until previous data is sent
	 */
	if (fl_send_data)
		return 0;

	/*
	 * Continuous acquisition stopped, drop the acquired data.
	 */
	if ((daq_time_base.mode == DAQ_ACQ_MODE_CONT) && (!daq_main.run))
		return 0;

	/*
	 * Copy acquired data to response data buffer. Which part of acquisition
	 * buffer will be copied depends on trigger mode.
	 *
	 *  'auto'   - copy all acquired samples.
	 *  'normal' - copy only pre-trigger and post-trigger samples around
	 *             trigger.
	 */
	if (daq_trigger.mode == DAQ_TRIG_AUTO) {
		int i, j;

		num = daq_buffer.sn;

		for (i = 0, j = daq_buffer.head; i < num; i++) {
			daq_resp_buf[i] = daq_buffer.buf[j];

			/* Acquisition buffer is circular one */
			if (++j >= DAQ_BUF_SIZE)
				j = 0;
		}
	} else {
		int i, j, last;

		num = daq_time_base.pre + daq_time_base.post;
		last = daq_buffer.tail > 0 ? daq_buffer.tail - 1 :
		    DAQ_BUF_SIZE - 1;

		/* Copy from the last sample to the first one */
		for (i = num - 1, j = last; i >= 0; i--) {
			daq_resp_buf[i] = daq_buffer.buf[j];

			/* Acquisition buffer is circular one */
			if (--j < 0) 
				j = DAQ_BUF_SIZE - 1;
		}

	}

	/* Prepare response - only one packet is to be sent */
	mcom_prepare_generic_response(&daq_resp_data, "00",
	    daq_resp_buf, MCOM_DATA_TYPE_WORD, num);

	/* acquired data processed, release acquisition buffer */
	daq_buffer.ready = FALSE;

	/* Set id of module which is going to respond */
	daq_resp_data.id = MCOMM_DAQ_ID;

	fl_send_data = TRUE;

	return 0;
}

/*
 * Takes care of sending response to master. Two responses might be scheduled
 * for transmission - ack/nak one and data one.
 * ack/nak response takes precedence over data response, since it is short,
 * thus it will occupy transmitter for shorter time.
 */
static int
daq_comm_svc()
{
	/*
	 * There is ack/nak response to be sent to master, attempt to queue it
	 * for transmission.
	 */
	if (fl_send_ack_nak) {
		if (mcom_queue_response(&daq_resp_ack_nak) != 0) {
			return EAGAIN;
		}

		fl_send_ack_nak = FALSE;

		/*
		 * Since transmitter is now busy, there is no point to continue
		 * with attempt to send data response. thus return.
		 */
		return 0;
	}

	/*
	 * There is data response to be sent to master, attempt to queue it
	 * for transmission.
	 */
	if (fl_send_data) {
		if (mcom_queue_response(&daq_resp_data) != 0) {
			return EAGAIN;
		}

		fl_send_data = FALSE;
	}

	return 0;
}

/*
 * Converts configuration data in form of 0xvvvu (v - value, o - order)
 * to number.
 */
static unsigned long
daq_convert_vvvu(unsigned long data)
{
	int i;
	int units = data & 0x0f;
	unsigned long result = data >> 4;

	for (i = 0; i < units; i++)
		result *= 1000;

	return result;
}

/*
 * Processes configuration data. Validates provided data.
 * Returns non-zero value if provided configuration is invalid.
 */
static int
daq_process_conf_cmd(char *cfg, int ln)
{
	int acq_mode;
	int trigger_mode;
	int trigger_event;
	int trigger_par;
	unsigned int sample_rate;
	int refresh_rate;
	int pre_num;
	int post_num;

	/* malformed packet */
	if (ln != 28)
		return EINVAL;

	/* cfg[0..1] - channel - not used for now */

	/* cfg[2..3] - acquisition mode */
	acq_mode = (int) hex2ulong(cfg + 2, 2);
	if ((acq_mode != DAQ_ACQ_MODE_SINGLE) &&
	    (acq_mode != DAQ_ACQ_MODE_CONT))
		return EINVAL;

	/* cfg[4..5] - trigger mode */
	trigger_mode = (int) hex2ulong(cfg + 4, 2);
	if ((trigger_mode != DAQ_TRIG_NORMAL) &&
	    (trigger_mode != DAQ_TRIG_AUTO))
		return EINVAL;
	
	/* cfg[6..7] - trigger event */
	trigger_event = (int) hex2ulong(cfg + 6, 2);
	if ((trigger_event != DAQ_TRIG_REDGE) &&
	    (trigger_event != DAQ_TRIG_FEDGE))
		return EINVAL;

	/* cfg[8..11] - trigger parameters */
	trigger_par = (int) hex2ulong(cfg + 8, 4);
	if (trigger_par > 0x3ff)
		return EINVAL;

	/* cfg[12..15] - sample rate */
	sample_rate = (unsigned int) daq_convert_vvvu(hex2ulong(cfg + 12, 4));
	if (sample_rate < 200)
		return EINVAL;

	/* cfg[16..19] - refresh rate */
	refresh_rate = (unsigned int) daq_convert_vvvu(hex2ulong(cfg + 16, 4));

	/* cfg[20..23] - number of pre-trigger samples */
	pre_num = (int) hex2ulong(cfg + 20, 4);
	if (pre_num > 0x400)
		return EINVAL;

	/* cfg[24..27] - number of post-trigger samples */
	post_num = (int) hex2ulong(cfg + 24, 4);
	if (post_num > 0x400)
		return EINVAL;

	if (pre_num + post_num > 0x400)
		return EINVAL;

	/*
	 * Everything seems to be correct, move configuration
	 * to appropriate structures
	 */
	daq_main.acq_per = refresh_rate;

	daq_trigger.mode = trigger_mode;
	daq_trigger.event = trigger_event;
	daq_trigger.lvl = trigger_par;

	daq_time_base.srate = sample_rate;
	daq_time_base.mode = acq_mode;
	daq_time_base.pre = pre_num;
	daq_time_base.post = post_num;

	return 0;
}

/* public functions */
/*
 * Initialization routine.
 */
int
daq_init(void)
{
	/* Initialize AD converter */
	/*
	 *  - configure all analog/digital pins as digital ones.
	 *  - AN8 is the only exception, it is used to measure car voltage.
	 *  - manual sampling mode
	 *  - clearing SAMP bit ends sampling and starts conversion
	 *  - tAD = tPB * 2 * (14 + 1) = 1us
	 *  - channel 8 on MUXA selected for conversion
	 *  - dummy conversion initiated
	 */

	/*
	 * A pin is configured as an analog input when the corresponding
	 * PCFGn bit (AD1PCFG<n>) = 0.
	 */
	AD1PCFG = 0xfeff;

	/*
	 * The port pins that are desired as analog inputs must have their
	 * corresponding TRIS bit set, specifying the pin as an input.
	 */
	TRISBSET = 0x0100;

	AD1CON2 = 0;
	AD1CON3 = (1 << 8) | 14;
	AD1CHS = 8 << 16;
	AD1CON1 = ADC_MODULE_ON;

	/* channel to be measured */
	daq_adc_ch = ADC_CH_VBAT;

	/* initialize communication data */
	daq_resp_ack_nak.id = MCOMM_DAQ_ID;
	daq_resp_data.id = MCOMM_DAQ_ID;

	/* initialize data acquisition structures to default values */
	/* trigger: auto */
	daq_trigger.mode = DAQ_TRIG_AUTO;
	/* rising edge, level to 33% - ignored in 'auto' mode */
	daq_trigger.event = DAQ_TRIG_REDGE;
	daq_trigger.lvl = 0x3ff / 5;

	/*
	 * time base:
	 *  - sample rate 1ms
	 *  - single acquisition mode
	 */
	daq_time_base.srate = 200;
	daq_time_base.pre = 400;
	daq_time_base.post = 400;
	daq_time_base.mode = DAQ_ACQ_MODE_CONT;

	/* prepare acquisition buffer */
	daq_buffer.head = daq_buffer.tail = daq_buffer.sn = 0;
	daq_buffer.fill = FALSE;
	daq_buffer.ready = FALSE;

	/* set period of running acquisition cycles to 1 second */
	daq_main.acq_per = 1000000;

	/* Initialize timers */
	daq_refresh_timer = NULL;
	rtl_timer_init(&daq_refresh_timer, 0);

	/* oscilloscope is inactive */
	daq_main.scope_st = DAQ_SCOPE_ST_IDLE;
	daq_main.run = FALSE;

	/* Kick off conversion process */
	/* select channel */
	AD1CHS = daq_adc_ch << 16;

	/* Initiate AD sampling phase. */
	AD1CON1SET = ADC_SAMP_ON;
	daq_adc_st = ADC_ST_SAMPLE;

	daq_adc_sample_rate = daq_time_base.srate / RTL_SERVICE_PERIOD;
	daq_adc_sample_acquired = FALSE;

	return 0;
}

/*
 * Suspend the module.
 */
int
daq_suspend(void)
{
	/* Turn off ADC module */
	AD1CON1 = ADC_MODULE_OFF;

	return 0;
}

/*
 * Provides raw data acquired via A/D converter.
 *
 * Returns EINVAL if ADC channel is out of range.
 */
int
daq_get_adc_value(unsigned int ch, unsigned int *result)
{
	/* make sure channel is in expected range */
	if (ch >= ADC_NUM_CHANNELS) {
		return EINVAL;
	}

	*result = adc_data[ch];

	return 0;
}

/*
 * Main service routine - called periodically from timer interrupt.
 */
int
daq_svc(void)
{
	/* Collect data from sensors connected to internal AD converter */
	daq_adc_svc();

	/* Handle 'oscilloscope' module */
	daq_scope_svc();

	/* Process acquired data */
	daq_post_process_svc();

	/* Send response to master */
	daq_comm_svc();

	return 0;
}

/*
 * Processes query from master targeted to acquisition module
 */
int
daq_acq_process_query(char *data, int ln)
{
	int err;
	BOOL response_with_ack = TRUE;

	/* Handle particular commands */
	switch (data[0]) {
		/* Configure acquisition module */
		case MCOMM_DAQ_CMD_CONF:
			/* configure module */
			err = daq_process_conf_cmd(data + 1, ln - 1);
			if (err != 0)
				response_with_ack = FALSE;
			break;

		/* Run acquisition */
		case MCOMM_DAQ_CMD_RUN:
			daq_main.run = TRUE;

			break;

		/* Stop acquisition */
		case MCOMM_DAQ_CMD_STOP:
			daq_main.run = FALSE;

			break;

		/* unknown command */
		default:
			err = MCOM_NAK_UNKNOWN_IDCMD;
			response_with_ack = FALSE;

			break;
	}

	/*
	 * Send response to master indicating that command was processed.
	 */
	if (response_with_ack) {
		mcom_prepare_ack_response(&daq_resp_ack_nak);
	} else {
		mcom_prepare_nak_response(&daq_resp_ack_nak, err);
	}

	/* Set id of module which is going to respond */
	daq_resp_ack_nak.id = MCOMM_DAQ_ID;

	fl_send_ack_nak = TRUE;

	return 0;
}

/*
 * Processes query from master targeted to A/D low level module. For now
 * it only supports reading direct value from ADC.
 */
int
daq_adc_process_query(char *data, int ln)
{
	unsigned int res;

	/* Module already sending a packet, abort */
	if (fl_send_data)
		return MCOM_NAK_MODULE_BUSY;

	if (daq_get_adc_value(hex2ulong(data, 2), &res) == 0) {
		mcom_prepare_generic_response(&daq_resp_ack_nak, "00",
		    &res, MCOM_DATA_TYPE_WORD, 1);
	} else {
		mcom_prepare_nak_response(&daq_resp_ack_nak, MCOM_NAK_ADC_ERR);
	}

	/* Set id of module which is going to respond */
	daq_resp_ack_nak.id = MCOMM_ADC_ID;

	fl_send_ack_nak = TRUE;

	return 0;
}

