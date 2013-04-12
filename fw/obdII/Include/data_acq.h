/*********************************************************************
 *
 * Module collecting data from various sensors connected directly to
 * PIC module (not via ECU).
 *
 * For now, only battery voltage is measured via 10bit AD converter.
 *
 *********************************************************************/

#ifndef __DATA_ACQ_H
#define __DATA_ACQ_H

#include <GenericTypeDefs.h>

#include "master_comm.h"

/* constants */
/* acquisition module commands */
#define	MCOMM_DAQ_CMD_CONF	'C'	/* configure */
#define	MCOMM_DAQ_CMD_RUN	'R'	/* run acquisition */
#define	MCOMM_DAQ_CMD_STOP	'S'	/* stop acquisition */
#define	MCOMM_PP_CMD_CONF	'P'	/* configure post processing module */

/* post processing modules */
#define DAQ_PP_OFF		0x00	/* no post processing done */
#define DAQ_PP_FFT		0x01	/* Fast Fourier Transformation */
#define DAQ_PP_SIGID		0x02	/* Characterize acquired signal */

/* Post processing commands */
#define DAQ_PP_CMD_ACTIVATE	'A'	/* Activate module */
#define DAQ_PP_CMD_TEST		'T'	/* Test module */

/* size of acquisition buffer */
#define	DAQ_ACQ_BUF_SIZE	1024

/* size of post processing buffer */
#define	DAQ_PP_BUF_SIZE		2048

/* acquisition mode */
#define	DAQ_ACQ_MODE_SINGLE	0x01	/* single */
#define	DAQ_ACQ_MODE_CONT	0x02	/* continuous */

/* trigger mode */
#define	DAQ_TRIG_NORMAL	0x01	/* normal */
#define	DAQ_TRIG_AUTO	0x02	/* auto */

/* trigger event */
#define	DAQ_TRIG_REDGE	0x01	/* rising edge */
#define	DAQ_TRIG_FEDGE	0x02	/* falling edge */

/* sample rate units */
#define	DAQ_SRATE_UNITS_US	0x00	/* microseconds */
#define	DAQ_SRATE_UNITS_MS	0x01	/* milliseconds */
#define	DAQ_SRATE_UNITS_SEC	0x02	/* seconds */

/* types */
/* structure encapsulating common acquisition data */
typedef struct daq_main {
	/* scope state machine */
	int scope_st;

	/* trigger state machine */
	int trigger_st;

	/*
	 * When operating in 'continuous' acquisition mode,
	 * contains period of acquisition cycles in microseconds
	 */
	int	acq_per;

	/* If TRUE, acquisition is running */
	BOOL 	run;
} daq_main_t;

/* structure encapsulating trigger related data */
typedef struct daq_trigger {
	int	mode;		/* mode - normal, auto */
	int	event;		/* event - rising/falling edge */
	int	lvl;		/* trigger level */

	BOOL	trigger_found;	/* trigger event found */
} daq_trigger_t;

/* structure encapsulating time base related data */
typedef struct daq_time_base {
	unsigned int	srate;		/* sample rate in microseconds */
	int		mode;		/* acq. mode - single or continuous */
	int		pre;		/* number of pre-trigger samples */
	int		post;		/* number of post-trigger samples */
} daq_time_base_t;

/* structure encapsulating acquisition buffer data */
typedef struct daq_buffer {
	unsigned int	buf[DAQ_ACQ_BUF_SIZE];	/* acquisition buffer */
	int		head;			/* head index */
	int		tail;			/* tail index */
	int		trig;			/* trigger position */
	int		sn;			/* number of acquired samples */

	BOOL		fill;		/* TRUE enables filling the buffer */
	BOOL		ready;		/* TRUE when acquisition is done */
} daq_buffer_t;

/* initialization routine */
extern int daq_init(void);

/* Processes query from master  targeted to acquisition module */
extern int daq_acq_process_query(char *, int);

/* Processes query from master targeted to A/D low level module */
extern int daq_adc_process_query(char *, int);

/* Main service routine - called periodically from timer interrupt. */
extern int daq_svc(void);
/* Provides data measured by A/D converter */
extern int daq_get_adc_value(unsigned int, unsigned int *);
/* Upper half of post processing service routine - called from main(). */
extern int daq_pp_upper_svc(void);

/* Suspend the module. */
extern int daq_suspend(void);

#endif /* __DATA_ACQ_H */

