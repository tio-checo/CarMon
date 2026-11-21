#ifndef CARMON_MEDIA_H
#define CARMON_MEDIA_H
#include <stdbool.h>
#include <stdint.h>
/* Calls are serialized by the application task; never call from an ISR. */
void carmon_media_init(void);
/* Cached initialization/I/O status, not physical card-detect state. */
bool carmon_media_present(void);
/* Transfer exactly one 512-byte sector; data must hold at least 512 bytes. */
bool carmon_media_read_sector(uint64_t sector, uint8_t *data);
bool carmon_media_write_sector(uint64_t sector, const void *data, bool allow_zero);
/* Number of sectors, or zero when unavailable. */
uint64_t carmon_media_capacity(void);
#endif
