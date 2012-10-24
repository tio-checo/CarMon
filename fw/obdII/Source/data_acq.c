/*********************************************************************
 *
 * Module collecting data from various sensors connected directly to
 * PIC module (not via ECU).
 *
 * For now, only battery voltage is measured via 10bit AD converter.
 *
 *********************************************************************/

#include "adc10.h"

#include "data_acq.h"

#include <errno.h>

/* local constants */
#define ADC_NUM_CHANNELS	10	/* total number of analog channels */
#define ADC_CH_VBAT		8	/* battery voltage measured on AN8 */

/* states of state machines collecting data from A/D converter */
#define ADC_ST_IDLE		0	/* state machine idle */
#define ADC_ST_SAMPLE		1	/* sampling phase */
#define ADC_ST_CONVERT		2	/* conversion phase */

/* local variables */
static unsigned int adc_data[ADC_NUM_CHANNELS];	/* measured ADC values */
static int daq_adc_st;				/* ADC state machine */
static int daq_adc_ch;				/* measured channel */

/* local functions */
/*
 * Service routine collecting data from sensors connected to PIC
 * 10bit AD converter.
 * For now only measures battery voltage - connected to analog channel 8.
 */
static int
daq_adc_service(void)
{
	switch (daq_adc_st) {
	/* machine idle */
	case ADC_ST_IDLE:
		break;

	/* sampling phase */
	case ADC_ST_SAMPLE:
		/* Initiate conversion phase */
		AD1CON1CLR = ADC_SAMP_ON;

		/* move to conversion phase */
		daq_adc_st++;

		break;

	/* conversion phase */
	case ADC_ST_CONVERT:
		/* wait until conversion is done */
		if (!(AD1CON1 & ADC_CONVERSION_DONE))
			break;

		/* clear 'conversion done' flag */
		AD1CON1CLR = ADC_CONVERSION_DONE;

		/* store measured value */
		adc_data[daq_adc_ch] = ADC1BUF0;

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

	/* Kick off conversion process */
	/* select channel */
	AD1CHS = daq_adc_ch << 16;

	/* Initiate AD sampling phase. */
	AD1CON1SET = ADC_SAMP_ON;
	daq_adc_st = ADC_ST_SAMPLE;

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
daq_get_adc_value(unsigned int ch, unsigned long *result)
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
daq_service(void)
{
	/* Collect data from sensors connected to internal AD converter */
	daq_adc_service();

	return 0;
}

