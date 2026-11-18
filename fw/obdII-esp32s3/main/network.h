/*********************************************************************
 *
 * Network startup using menuconfig settings.
 *
 *********************************************************************/

#ifndef __NETWORK_H
#define __NETWORK_H

#include "esp_err.h"

/* Start networking in a separate task on CPU0; no USB input is needed. */
extern esp_err_t network_init(void);

#endif /* __NETWORK_H */
