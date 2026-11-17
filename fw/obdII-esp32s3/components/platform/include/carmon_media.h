#ifndef CARMON_MEDIA_H
#define CARMON_MEDIA_H
#include <stdbool.h>
#include <stdint.h>
void carmon_media_init(void);
bool carmon_media_present(void);
bool carmon_media_read_sector(uint64_t sector, uint8_t *data);
bool carmon_media_write_sector(uint64_t sector, const void *data, bool allow_zero);
uint64_t carmon_media_capacity(void);
#endif
