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

/* initialization routine */
extern int daq_init(void);

/* Main service routine - called periodically from timer interrupt. */
extern int daq_service(void);
/* Provides data measured by A/D converter */
extern int daq_get_adc_value(unsigned int, unsigned long *);

/* Suspend the module. */
extern int daq_suspend(void);

#endif /* __DATA_ACQ_H */

