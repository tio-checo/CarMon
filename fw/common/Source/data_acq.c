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
#include "bt.h"
#include "dsplib_dsp.h"
#include "fftc.h"

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

/* states of post processing state machine */
#define	DAQ_PP_ST_IDLE		0	/* machine idle */
#define	DAQ_PP_ST_WAIT4DATA	1	/* wait for acquired data */
#define	DAQ_PP_ST_FFT_CALC	2	/* FFT - calculate */
#define	DAQ_PP_ST_SIGID_CALC	3	/* signal identification - calculate */

/* FFT */
#define	FFT_LOG2N	11
#define	FFT_N		(1 << FFT_LOG2N)

/* Signal identification */
#define	DAQ_PP_MAX_FREQ_SNUM	400
#define	DAQ_PP_MAX_FREQ_FNUM	5

/* test sequences */
//static const char tst1[] = "02DE02DE02DE02DE02DF02DE02DE02DE02DE02DE02DF02DE02DE02DF02DF02DF02DF02DF02DF02DF02DF02DE02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DE02DD02DC02DA02DA02DC02DC02DC02DB02DC02DC02DE02DE02DF02DF02DF02DF02DF02DF02DE02DF02DE02DF02DE02DE02DE02DD02DE02DD02DE02DF02DF02DF02DF02DF02E002DF02E002DF02DF02DF02E002E002DF02DF02DF02DF02DE02DE02DF02DF02DD02DC02DA02DA02DD02DE02DD02DD02DC02DD02DE02DE02DE02DE02DE02DE02DE02DD02DE02DF02DF02DE02DE02DE02DE02DE02DF02DF02DE02DF02DF02DF02DF02DF02DF02DF02DE02DE02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DE02DD02DB02D902DA02DC02DD02DC02DB02DC02DC02DE02DF02DF02DF02DF02DF02DF02DF02DE02DE02DE02DE02DE02DE02DD02DD02DE02DD02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02E002DF02DE02DF02DF02DE02DE02DF02DE02DD02DB02D902DA02DC02DD02DD02DC02DC02DC02DF02DF02DF02DE02DF02DE02DE02DD02DE02DE02DE02DE02DE02DE02DE02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DE02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DD02DC02D902DA02DC02DC02DC02DB02DC02DC02DE02DF02DF02DF02DF02DF02DF02DF02DE02DE02DE02DE02DE02DE02DD02DE02DD02DD02DE02DF02DE02DF02DF02DF02E002E002DF02DF02DF02DF02DF02E002DF02DF02DF02DE02DF02DE02DF02DE02DE02DC02DA02DB02DD02DD02DD02DC02DC02DD02DE02DF02DF02DF02DF02DE02DF02DE02DE02DF02DE02DE02DE02DE02DE02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DE02DC02D902DA02DD02DC02DC02DB02DC02DC02DE02DE02DF02DF02DF02DF02DF02DF02DE02DF02DE02DE02DE02DD02DD02DE02DD02DD02DE02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DE02DE02DE02DE02DE02DF02DE02DD02DB02DA02DA02DD02DD02DD02DD02DC02DD02DE02DE02DE02DE02DE02DE02DE02DD02DE02DE02DE02DE02DE02DE02DE02DF02DE02DE02DE02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02E002DF02DF02DF02DF02DF02DF02DF02DE02DE02DB02D902DA02DD02DD02DC02DB02DC02DD02DE02DF02DF02DF02DF02DF02DF02DF02DE02DF02DF02DE02DE02DE02DE02DD02DD02DD02DF02DF02DF02DF02DF02DF02DF02E002DF02E002DF02E002DF02E002DF02DF02DF02DF02DE02DE02DF02DE02DD02DB02DA02DB02DE02DE02DD02DC02DC02DD02DE02DF02DF02DE02DE02DE02DD02DD02DE02DE02DE02DE02DE02DE02DE02DF02DE02DF";
static const char tst2[] = "02DD02DD02DD02DC02DB02DB02DB02DC02DC02DC02DC02DC02DB02DB02DB02DB02DB02DB02DB02DB02DB02DA02DA02D902D902D802DA02DA02D902DA02DA02DA02DB02DA02DB02DA02DA02D902DA02DA02D902D902D902D902D902D902D802D902D802D802D702D802D702D602D302D202D302D502D802D702D702D602D502D602D702D702D702D602D502D502D702D802D902D902D902D802D802D902D902D802D702D802D702D802D802D802D902D902DA02D902D902D902DA02DA02DA02DA02DA02D902DA02DA02D902D902D902D802D802D802D902D902D902D902D902DA02DA02DA02DB02DA02DA02DA02DC02DB02DB02DB02DB02DA02DA02D902DA02DB02DB02DB02DB02DB02DB02DC02DD02DD02DD02DD02DE02DE02DE02DD02DD02DD02DD02DD02DE02DD02DE02DD02DC02DD02DD02DD02DE02DE02DE02DE02DF02DF02DF02DF02DE02DE02DE02DE02DE02DF02DF02DF02DF02DF02DF02DE02DE02DE02DE02DE02DF02DE02DE02DE02DE02DD02DD02DD02DE02DD02DD02DD02DD02DD02DC02DD02DC02DD02DC02DB02DC02DC02DB02DB02DB02DA02DA02D902DA02DA02DB02DA02DB02DA02DA02D902D902D902D902D802DA02D902D902D902D802D802D802D702D802D802D802D702D702D802D802D902D902D902D902D902DA02D902D802D602D302D402D602D702D802D602D502D402D402D602D602D502D402D302D302D502D702D802D802D802D802D902D802D702D802D802D902D802D902D902D902D902D902D802D802D802D902D902D802D902D802D802D802D902D902DA02DA02DB02DA02DA02DA02DB02DB02DB02DB02DA02DB02DA02D902D902D902D902D902D902DA02DA02DA02DB02DB02DC02DC02DC02DC02DC02DC02DC02DD02DD02DD02DD02DD02DD02DC02DB02DD02DD02DD02DD02DE02DF02DF02DF02DF02DF02DF02DF02DF02E002DF02DF02DF02DF02DF02DF02DF02E002DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DF02DE02DE02DE02DE02DD02DD02DD02DE02DD02DD02DE02DD02DD02DD02DD02DD02DD02DD02DD02DD02DC02DC02DC02DB02DB02DC02DB02DB02DB02DB02DB02DB02DB02DB02DB02DB02DA02DB02DA02D902D902D902D902D902D802D802DA02D902D902D902D902D902D902D902D802D802D802D902D902D902D802D802D702D702D602D702D602D402D302D302D502D702D702D602D602D602D702D902D902D702D602D502D502D602D702D902D802D902D802D802D702D802D702D702D702D702D702D802D902D802D802D902D802D802D802D902D902D902DA02DA02DA02DA02DA02D902D902D902D902DA02D902D902D902D902D902D902D902DB02DB02DB02DB02DB02DB02DB02DC02DB02DB02DB02DB02DB02DB02DA02DA02DA02DA02DA02D902DB02DB02DC02DC02DD02DC";

/* local variables */
static unsigned int adc_data[ADC_NUM_CHANNELS];	/* measured ADC values */
static int daq_adc_st;				/* ADC state machine */
static int daq_adc_ch;				/* measured channel */
static BOOL daq_adc_sample_acquired;		/* new sample available */

/* oscilloscope module */
static daq_main_t	daq_main;	/* main */
static daq_trigger_t	daq_trigger;	/* trigger */
static daq_time_base_t	daq_time_base;	/* time base */
static daq_buffer_t	daq_buffer;	/* acquisition buffer */

/* post processing module */
static int daq_pp_st;			/* post processing state machine */
static int daq_pp_mode;			/* post processing mode */
static BOOL fl_pp_run_upper;		/* run pp upper layer */
/*
 * Structure encapsulating data participating in post processing.
 */
static struct daq_pp_t {
	int	buf[DAQ_PP_BUF_SIZE];	/* input/output buffer */
	int	ln;			/* buffer usage */

	int	mean;			/* calculated mean value */
	int	rms;			/* calculated rms value of AC part */
	int	min, max;		/* minimum/maximum value of AC part */
	int	f[DAQ_PP_MAX_FREQ_FNUM];	/* strongest frequencies */
	int	m[DAQ_PP_MAX_FREQ_FNUM];

	/* buffers used by FFT */
	int16c	fft_in[FFT_N];
	int16c	fft_out[FFT_N];
	int16c	fft_scratch[FFT_N];
} daq_pp;

/* communication module */
/* structure holding ACK/NAK response to master */
static mcom_response_t daq_resp_ack_nak;
/* If set to TRUE, send ack/nak response packet to master */
static BOOL fl_send_ack_nak;

/* structure holding data response to master */
static mcom_response_t daq_resp_data;
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
 * Calculates square root of given number. Following iterative formula
 * is used to calculate the value:
 *
 * y[n] = (y[n-1] + x[n-1] / y[n-1]) / 2
 *
 * Iteration process ends once difference between y[n] and y[n-1]
 * is less than 2. The method converges in approximately 10 steps.
 */
static int
daq_sqrt(int x)
{
	int y1 = 1 << 12;
	int y2;
	int n = 0;
	int d = 0;

	/* Only positive 'x' are valid input. */
	if (x < 0)
		return 0;

	/* Handle small numbers separately */
	if (x < 3)
		return x;

	do {
		y2 = (y1 + x / y1) / 2;

		d = y1 > y2 ? (y1 - y2) : (y2 - y1);

		y1 = y2;
		n++;
	} while (d > 1 && n < 100);

	return y2;
}

/*
 * Sends data packet to master.
 */
static int
daq_send_data_response_to_master(void)
{
	mcom_prepare_generic_response(&daq_resp_data, "00",
	    daq_pp.buf, MCOM_DATA_TYPE_WORD, daq_pp.ln);

	/* Set id of module which is going to respond */
	daq_resp_data.id = MCOMM_DAQ_ID;

	fl_send_data = TRUE;

	return 0;
}

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
			daq_sr_timer = daq_time_base.srate / RTL_SERVICE_PERIOD;
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
		i += DAQ_ACQ_BUF_SIZE;
	} else if (i > DAQ_ACQ_BUF_SIZE) {
		i -= DAQ_ACQ_BUF_SIZE;
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
		if (++daq_buffer.sn < DAQ_ACQ_BUF_SIZE) {
			daq_buffer.tail++;
		} else {
			daq_buffer.sn = DAQ_ACQ_BUF_SIZE;

			/* Buffer is full, move both head and tail markers */
			if (++daq_buffer.tail >= DAQ_ACQ_BUF_SIZE) {
				daq_buffer.tail = 0;
			}
			if (++daq_buffer.head >= DAQ_ACQ_BUF_SIZE) {
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

		/* Connection with master lost, cancel acquisition. */
		if (!bt_is_paired()) {
			daq_main.run = FALSE;
			break;
		}

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
 * Bottom half of post processing service. Processes acquired data before
 * they are either sent to master or passed to upper half of post processing
 * service.
 */
static int
daq_pp_bottom_svc()
{
	int num;

	/* Wait until upper layer finishes its job */
	if (fl_pp_run_upper)
		return 0;

	/* There are no data to be processed, return */
	if (!daq_buffer.ready)
		return 0;

	/* Wait until sending previous data to the master is completed. */
	if (fl_send_data)
		return 0;

	/* Continuous acquisition aborted, drop the acquired data. */
	if ((daq_time_base.mode == DAQ_ACQ_MODE_CONT) && (!daq_main.run))
		return 0;

	/*
	 * Copy acquired data to post processing data buffer. Which part
	 * of acquisition buffer will be copied depends on trigger mode.
	 *
	 *  'auto'   - copy all acquired samples.
	 *  'normal' - copy only pre-trigger and post-trigger samples around
	 *             trigger. Treat zero number of pre-trigger samples
         *             in special way. In that case, copy all acquired samples.
	 */
	if ((daq_trigger.mode == DAQ_TRIG_AUTO) || (daq_time_base.pre == 0)) {
		int i, j;

		num = daq_buffer.sn;

		for (i = 0, j = daq_buffer.head; i < num; i++) {
			daq_pp.buf[i] = daq_buffer.buf[j];

			/* Acquisition buffer is circular one */
			if (++j >= DAQ_ACQ_BUF_SIZE)
				j = 0;
		}
	} else {
		int i, j, last;

		num = daq_time_base.pre + daq_time_base.post;
		last = daq_buffer.tail > 0 ? daq_buffer.tail - 1 :
		    DAQ_ACQ_BUF_SIZE - 1;

		/* Copy from the last sample to the first one */
		for (i = num - 1, j = last; i >= 0; i--) {
			daq_pp.buf[i] = daq_buffer.buf[j];

			/* Acquisition buffer is circular one */
			if (--j < 0) 
				j = DAQ_ACQ_BUF_SIZE - 1;
		}

	}

	/* Save number of buffer usage - used by post processing module. */
	daq_pp.ln = num;

	/* Let post processing upper layer process acquired data. */
	if (daq_pp_mode != DAQ_PP_OFF) {
		fl_pp_run_upper = TRUE;

		return 0;
	}

	/* Post processing is turned off, send the raw data to master. */
	daq_send_data_response_to_master();

	/* acquired data processed, release acquisition buffer */
	daq_buffer.ready = FALSE;

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

/*
 * Calculates mean value.
 */
static int
daq_pp_mean(void)
{
	int i;
	int mean = 0;
	int ln = daq_pp.ln;

	/* Calculate mean value of acquired data */
	for (i = 0; i < ln; i++)
		mean += daq_pp.buf[i];

	daq_pp.mean = mean / ln;

	return 0;
}

/*
 * Calculates RMS value 
 */
static int
daq_pp_rms(void)
{
	int i;
	int rms = 0;
	int ln = daq_pp.ln;

	/* Calculate RMS value of acquired data */
	for (i = 0; i < ln; i++)
		rms += daq_pp.buf[i] * daq_pp.buf[i];

	/* TODO: calculate root of the value */
	daq_pp.rms = daq_sqrt(rms / ln);

	return 0;
}

/*
 * Calculates minimum and maximum values
 */
static int
daq_pp_min_max(void)
{
	int i;
	int min;
	int max;
	int ln = daq_pp.ln;

	max = min = daq_pp.buf[0];
	for (i = 1; i < ln; i++)
		if (daq_pp.buf[i] < min)
			min = daq_pp.buf[i];
		else if (daq_pp.buf[i] > max)
			max = daq_pp.buf[i];

	daq_pp.min = min;
	daq_pp.max = max;

	return 0;
}

/*
 * Removes DC bias (mean value) from acquired signal. 
 */
static int
daq_pp_remove_dc_bias(void)
{
	int i;
	int ln = daq_pp.ln;
	int mean = daq_pp.mean;

	for (i = 0; i < ln; i++)
		daq_pp.buf[i] -= mean;

	return 0;
}

/*
 * Calculates FFT.
 */
static int
daq_pp_calc_fft(void)
{
	int i;
	int16 max, s;

//	LATBCLR = 1 << 12;

	/* Calculate mean value of acquired data. */
	daq_pp_mean();

	/* Remove DC bias from acquired data. */
	daq_pp_remove_dc_bias();

//	LATBSET = 1 << 12;

	/* Calculate minimum and maximum value of AC part. */
	daq_pp_min_max();

	/* Calculate RMS value of AC part. */
	daq_pp_rms();

	/*
	 * Populate FFT input buffer. Scale input data, so that FFT can generate
	 * usable spectrum.
	 */
	/* Compute scaling factor */
	max = daq_pp.min >= 0 ? daq_pp.min : -daq_pp.min;
	s = daq_pp.max >= 0 ? daq_pp.max : -daq_pp.max;

	if (s > max)
		max = s;

	/*
	 * There is no point to calculate FFT for really small signals, as
	 * it would produce incorrect results anyway.
	 */
	if (max < 2) {
		/* Zero output buffer */
		for (i = 0; i < FFT_N; i++)
			daq_pp.fft_out[i].re = daq_pp.fft_out[i].im = 0;

		return EINVAL;
	}

	s = 10000 / max;

	for (i = 0; i < daq_pp.ln; i++) {
		daq_pp.fft_in[i].re = ((int16) daq_pp.buf[i]) * s;
		daq_pp.fft_in[i].im = 0;
	}
	/* Zero the rest of the buffer */
	for (i = daq_pp.ln; i < FFT_N; i++) {
		daq_pp.fft_in[i].re = daq_pp.fft_in[i].im = 0;
	}

//	LATBCLR = 1 << 12;

	/* Calculate FFT. */
	mips_fft16(daq_pp.fft_out, daq_pp.fft_in, (int16c *)fft16c2048,
	    daq_pp.fft_scratch, FFT_LOG2N);


//	LATBSET = 1 << 12;

	return 0;
}

/*
 * Find strongest frequencies in calculated spectrum.
 */
static int
daq_pp_find_fft_max_freqs(void)
{
	int i, j, st;
	int m, f;
	int16c *in = daq_pp.fft_out;
	int *out = daq_pp.buf;

	/* 
	 * Calculate square of frequency magnitudes. We are interested just
	 * in first DAQ_PP_MAX_FREQ_SNUM values.
	 */
	for (i = 0; i < DAQ_PP_MAX_FREQ_SNUM; i++) {
		out[i] = (int) in[i].re * (int) in[i].re +
		   (int) in[i].im * (int) in[i].im;
	}

	/*
	 * Now go through the result and search for maximums.
	 */
	st = 0;
	for (i = 0; i < DAQ_PP_MAX_FREQ_FNUM; i++)
		daq_pp.f[i] = daq_pp.m[i] = 0;

	for (i = 1; i < DAQ_PP_MAX_FREQ_SNUM; i++) {
		switch (st) {
		/* Waiting for growing part of function */
		case 0:
			if (out[i] > out[i - 1])
				st = 1;

			break;

		/* Wait for maximum */
		case 1:
			if (out[i] >= out[i - 1])
				break;

			f = i - 1;
			m = out[f];

			/*
			 * Maximum found. Save it only if its magnitude is
			 * bigger than the ones already found. Take into
			 * account the fact that the list is sorted.
			 * If maximum is lower than the ones already found,
			 * discard it and restart searching process.
			 */
			if (daq_pp.m[DAQ_PP_MAX_FREQ_FNUM - 1] >= m) {
				st = 0;
				break;
			}

			/*
			 * Find place for the value discarding frequency
			 * with the lowest magnitude. Keep the list
			 * of frequencies sorted by magnitude.
			 */
			for (j = 0; j < DAQ_PP_MAX_FREQ_FNUM; j++) {
				int k;

				if (daq_pp.m[j] > m)
					continue;

				/* Place found, move existing values */
				for (k = DAQ_PP_MAX_FREQ_FNUM - 1; k > j; k--) {
					daq_pp.m[k] = daq_pp.m[k - 1];
					daq_pp.f[k] = daq_pp.f[k - 1];
				}

				break;
			}

			/* Save the frequency and magnitude */
			daq_pp.f[j] = f;
			daq_pp.m[j] = m;

			/* Restart the process */
			st = 0;

			break;
		}
	}

	/*
	 * Finish calculation of magnitudes for selected frequencies.
	 * In particular, we operated so far with squares of magnitudes,
	 * so calculate their square root. We postpone that operation until
	 * now to avoid calculations for frequencies we are not interested in.
	 */
	for (i = 0; i < DAQ_PP_MAX_FREQ_FNUM; i++)
		daq_pp.m[i] = daq_sqrt(daq_pp.m[i]);

	return 0;
}

/*
 * Move results of FFT process to the transmit buffer.
 */
static int
daq_pp_fft_prepare_result(void)
{
	int i, j;

	for (i = 0, j = DAQ_PP_BUF_SIZE / 2; i < DAQ_PP_BUF_SIZE / 2;
	    i++, j++) {
		daq_pp.buf[i] = daq_pp.fft_out[i].re;
		daq_pp.buf[j] = daq_pp.fft_out[i].im;
	}

	daq_pp.ln = DAQ_PP_BUF_SIZE;

	return 0;
}

/*
 * Handles FFT commands.
 */
static int
daq_process_fft_cmd(char *cfg, int ln)
{
	int i, lnt;

	/* cfg[0] - command */
	switch (cfg[0]) {
	/* Activate FFT module - nothing special to be done for now */
	case DAQ_PP_CMD_ACTIVATE:
		daq_pp_mode = DAQ_PP_FFT;
		break;

	/* Test FFT module - calculates FFT from given data */
	case DAQ_PP_CMD_TEST:
		/*
		 * Read input data to data response buffer. It is used
		 * as source of data for daq_pp_calc_fft() function.
		 * Data are passed as sequence of HEX ASCII quartets
		 * representing 16-bit unsigned values.
		 */
		lnt = sizeof (tst2);
		for (i = 0; i < lnt / 8; i++)
			daq_pp.buf[i] = hex2ulong((char *) (tst2 + i * 8),
			    4);

		/* Zero the rest of buffer */
		for (i = lnt / 8; i < DAQ_PP_BUF_SIZE; i++)
			daq_pp.buf[i] = 0;

		daq_pp.ln = lnt / 8;

		/* Calculate FFT of AC part of acquired data. */
		daq_pp_calc_fft();

		/*
		 * Move resulting data to response data buffer. The first half
		 * contains real part, the second half contains imaginary part.
		 */
		daq_pp_fft_prepare_result();

		/* Queue data for sending to master. */
		daq_send_data_response_to_master();

		break;

	/* unknown command */
	default:
		return EINVAL;
		/* NOT REACHED */
		break;
	}

	return 0;
}

/*
 * Move results of signal identification process to the transmit buffer.
 */
static int
daq_pp_sigid_prepare_result(void)
{
	int i;

	daq_pp.buf[0] = daq_pp.mean;
	daq_pp.buf[1] = daq_pp.rms;
	daq_pp.buf[2] = daq_pp.min;
	daq_pp.buf[3] = daq_pp.max;
	for (i = 0; i < DAQ_PP_MAX_FREQ_FNUM; i++) {
		daq_pp.buf[4 + i * 2] = daq_pp.f[i];
		daq_pp.buf[5 + i * 2] = daq_pp.m[i];
	}

	daq_pp.ln = 4 + DAQ_PP_MAX_FREQ_FNUM * 2;

	return 0;
}

/*
 * Handles signal identification commands.
 */
static int
daq_process_sigid_cmd(char *cfg, int ln)
{
	int i, lnt;

	/* cfg[0] - command */
	switch (cfg[0]) {
	/* Activate FFT module - nothing special to be done for now */
	case DAQ_PP_CMD_ACTIVATE:
		daq_pp_mode = DAQ_PP_SIGID;
		break;

	/* Test SIGID module - collect signal characteristics from test data */
	case DAQ_PP_CMD_TEST:
		/*
		 * Read test data to data response buffer. It is used
		 * as source of data for daq_pp_calc_fft() function.
		 * Data are passed as sequence of HEX ASCII quartets
		 * representing 16-bit unsigned values.
		 */
		lnt = sizeof (tst2);
		for (i = 0; i < lnt / 8; i++)
			daq_pp.buf[i] = hex2ulong((char *) (tst2 + i * 8),
			    4);

		/* Zero the rest of buffer */
		for (i = lnt / 8; i < DAQ_PP_BUF_SIZE; i++)
			daq_pp.buf[i] = 0;

		daq_pp.ln = lnt / 8;

//	LATBCLR = 1 << 12;
		/*
		 * Calculate FFT of AC part of acquired data. If FFT failed,
		 * there is no point to process the result.
		 */
		if (daq_pp_calc_fft() == 0) {
			/*
			 * Process result of FFT analysis. Find strongest
			 * frequencies.
			 */
			daq_pp_find_fft_max_freqs();
		} else {
			for (i = 0; i < DAQ_PP_MAX_FREQ_FNUM; i++)
				daq_pp.f[i] = daq_pp.m[i] = 0;
		}

		/* Move resulting data to response data buffer. */
		daq_pp_sigid_prepare_result();
//	LATBSET = 1 << 12;

		/* Queue data for sending to master. */
		daq_send_data_response_to_master();

		break;

	/* unknown command */
	default:
		return EINVAL;
		/* NOT REACHED */
		break;
	}

	return 0;
}



/*
 * Processes post processing configuration data. Validates provided data.
 * Returns non-zero value if provided configuration is invalid.
 */
static int
daq_process_pp_id(char *cfg, int ln)
{
	int pp_module;

	/* cfg[0..1] - post processing module ID */
	pp_module = (int) hex2ulong(cfg, 2);

	/* Turn post processing off */
	if (pp_module == DAQ_PP_OFF) {
		daq_pp_mode = pp_module;

		return 0;
	}

	if (pp_module == DAQ_PP_FFT)
		return daq_process_fft_cmd(cfg + 2, ln - 2);

	if (pp_module == DAQ_PP_SIGID)
		return daq_process_sigid_cmd(cfg + 2, ln - 2);

	/* Unknown module */
	return EINVAL;
}

/*
 * Main acquisition service routine - called periodically from timer interrupt.
 */
static int
daq_svc(void)
{
	/* Collect data from sensors connected to internal AD converter */
	daq_adc_svc();

	/* Handle 'oscilloscope' module */
	daq_scope_svc();

	/* Process acquired data */
	daq_pp_bottom_svc();

	/* Send response to master */
	daq_comm_svc();

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
	/* trigger: single */
	daq_trigger.mode = DAQ_TRIG_AUTO;
	/* rising edge, level to 33% - ignored in 'auto' mode */
	daq_trigger.event = DAQ_TRIG_REDGE;
	daq_trigger.lvl = 0x3ff / 5;

	/*
	 * time base:
	 *  - sample rate 400 us
	 *  - single acquisition mode
	 */
	daq_time_base.srate = 400;
	daq_time_base.pre = 128;
	daq_time_base.post = 128;
	daq_time_base.mode = DAQ_ACQ_MODE_SINGLE;

	/* prepare acquisition buffer */
	daq_buffer.head = daq_buffer.tail = daq_buffer.sn = 0;
	daq_buffer.fill = FALSE;
	daq_buffer.ready = FALSE;

	/* set period of running acquisition cycles to 100 millisecond */
	daq_main.acq_per = 100000;

	/* Initialize timers */
	daq_refresh_timer = NULL;
	rtl_timer_init(&daq_refresh_timer, 0);

	/* oscilloscope is inactive */
	daq_main.scope_st = DAQ_SCOPE_ST_IDLE;
	daq_main.run = FALSE;

	/* Initialize post processing module */
	daq_pp_st = DAQ_PP_ST_IDLE;
	daq_pp_mode = DAQ_PP_OFF;
	fl_pp_run_upper = FALSE;

	/* Kick off conversion process */
	/* select channel */
	AD1CHS = daq_adc_ch << 16;

	/* Initiate AD sampling phase. */
	AD1CON1SET = ADC_SAMP_ON;
	daq_adc_st = ADC_ST_SAMPLE;

	daq_adc_sample_acquired = FALSE;

	/* Register and enabled service. */
	rtl_svc_init("DataAcq", daq_svc, TRUE, FALSE);

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
 * Upper half of post processing service routine - called from main().
 */
int
daq_pp_upper_svc(void)
{
	int i;

	/* Upper layer stopped, make sure state machine is idle */
	if (!fl_pp_run_upper) {
		daq_pp_st = DAQ_PP_ST_IDLE;

		return 0;
	}

	/* Post processing turned off */
	if (daq_pp_mode == DAQ_PP_OFF)
		return 0;

	switch (daq_pp_st) {
	/* machine idle */
	case DAQ_PP_ST_IDLE:
		daq_pp_st = DAQ_PP_ST_WAIT4DATA;

		break;

	/* FFT - wait for data */
	case DAQ_PP_ST_WAIT4DATA:
		if (!daq_buffer.ready)
			break;

		/* FFT turned on */
		if (daq_pp_mode == DAQ_PP_FFT)
			daq_pp_st = DAQ_PP_ST_FFT_CALC;

		/* Signal identification turned on */
		else if (daq_pp_mode == DAQ_PP_SIGID)
			daq_pp_st = DAQ_PP_ST_SIGID_CALC;

		/* Post processing turned off */
		else
			daq_pp_st = DAQ_PP_ST_IDLE;

		break;

	/* FFT - calculate */
	case DAQ_PP_ST_FFT_CALC:
		/* Calculate */
		daq_pp_calc_fft();

		/*
		 * Move resulting data to response data buffer. The first half
		 * contains real part, the second half contains imaginary part.
		 */
		daq_pp_fft_prepare_result();

		/* Queue data for sending to master. */
		daq_send_data_response_to_master();

		/* acquired data processed, release acquisition buffer */
		daq_buffer.ready = FALSE;

		/* switch to IDLE mode */
		daq_pp_st = DAQ_PP_ST_IDLE;

		/* Let bottom layer know we are done. */
		fl_pp_run_upper = FALSE;

		break;

	/* Signal identification - calculate */
	case DAQ_PP_ST_SIGID_CALC:
//	LATBCLR = 1 << 12;

		/*
		 * Calculate FFT of AC part of acquired data. If FFT failed,
		 * there is no point to process the result.
		 */
		if (daq_pp_calc_fft() == 0) {
			/*
			 * Process result of FFT analysis. Find strongest
			 * frequencies.
			 */
			daq_pp_find_fft_max_freqs();
		} else {
			for (i = 0; i < DAQ_PP_MAX_FREQ_FNUM; i++)
				daq_pp.f[i] = daq_pp.m[i] = 0;
		}

//	LATBSET = 1 << 12;

		/* Move resulting data to response data buffer. */
		daq_pp_sigid_prepare_result();

		/* Queue data for sending to master. */
		daq_send_data_response_to_master();

		/* acquired data processed, release acquisition buffer */
		daq_buffer.ready = FALSE;

		/* switch to IDLE mode */
		daq_pp_st = DAQ_PP_ST_IDLE;

		/* Let bottom layer know we are done. */
		fl_pp_run_upper = FALSE;

		break;

	}

	return 0;
}

/*
 * Processes query from master targeted to acquisition module
 */
int
daq_acq_process_query(char *data, int ln)
{
	int err;
	BOOL respond_with_ack = TRUE;

	/* Handle particular commands */
	switch (data[0]) {
		/* Configure acquisition module */
		case MCOMM_DAQ_CMD_CONF:
			/* configure module */
			err = daq_process_conf_cmd(data + 1, ln - 1);
			if (err != 0)
				respond_with_ack = FALSE;
			break;

		/* Run acquisition */
		case MCOMM_DAQ_CMD_RUN:
			daq_main.run = TRUE;

			break;

		/* Stop acquisition */
		case MCOMM_DAQ_CMD_STOP:
			daq_main.run = FALSE;

			break;

		/* Configure post processing module */
		case MCOMM_PP_CMD_CONF:
			/* configure module */
			err = daq_process_pp_id(data + 1, ln - 1);
			if (err != 0)
				respond_with_ack = FALSE;
			break;

		/* unknown command */
		default:
			err = MCOM_NAK_UNKNOWN_IDCMD;
			respond_with_ack = FALSE;

			break;
	}

	/*
	 * Send response to master indicating that command was processed.
	 */
	if (respond_with_ack) {
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

