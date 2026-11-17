#pragma once

#include <stdbool.h>

/* Destructive test of a privately allocated block; never touches live data. */
bool psram_run_test(void);
