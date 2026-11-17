#pragma once

#include <stdbool.h>

/* Run once from a task with a >= 12 KiB stack after Wi-Fi has acquired an IP. */
bool internet_run_test(void);
