#ifndef CARMON_CAN_FRAME_H
#define CARMON_CAN_FRAME_H
#include <stdint.h>
/* Logical adapter only: this is NOT the layout of PIC32 or TWAI message RAM.
 * A future TWAI backend must populate the fields explicitly. */
typedef struct {
    struct { uint32_t SID; uint32_t CMSGTS; } msgSID;
    struct { uint32_t EID; uint32_t IDE; uint32_t RTR; uint32_t DLC; } msgEID;
    uint8_t data[8];
} carmon_can_frame_t;
#endif
