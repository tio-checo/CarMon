#ifndef CARMON_CAN_TWAI_H
#define	CARMON_CAN_TWAI_H

#include <stdint.h>
#include "can_hal.h"

/* Status flags retain the corresponding original protocol bits. */
#define	CAN_TWAI_STATUS_WARNING		0x01
#define	CAN_TWAI_STATUS_BUS_OFF		0x20
#define	CAN_TWAI_STATUS_RX_DROPPED	0x40
#define	CAN_TWAI_STATUS_INCOMPLETE	0x80

typedef struct {
	uint8_t flags;
	uint8_t tx_errors;
	uint8_t rx_errors;
} can_twai_status_t;

/*
 * Read controller status from the CAN/OBD task.
 * INCOMPLETE means error counters and directional flags are unavailable.
 */
extern int can_twai_get_status(can_twai_status_t *);

/*
 * Receive one frame without waiting.
 * Returns 0, EAGAIN when empty, or ENODEV before initialization.
 */
extern int can_twai_receive(can_frame_t *);

/*
 * Send one frame and wait for transmission completion.
 * Called from task context with exclusive access to CAN.
 * timeout_ms must be positive and specifies the completion wait.
 *
 * Returns:
 *   0          Frame transmitted successfully.
 *   EINVAL     Invalid frame pointer, identifier, data length,
 *              or nonpositive timeout.
 *   ENODEV     CAN node has not been created.
 *   EBUSY      Previous transmission is still pending, or the
 *              driver could not accept the new frame immediately.
 *   ENOTSUP    Transmission is not supported in the current mode,
 *              for example listen-only mode.
 *   ETIMEDOUT  Frame was accepted, but the completion wait expired.
 *   EIO        Transmission failed, a driver operation failed,
 *              or the completion state was inconsistent.
 *
 * A completion wait failure does not cancel the transmission,
 * stop the node, or release storage still used by the driver.
 * The frame may be transmitted after ETIMEDOUT is returned.
 *
 * Before submitting a new frame, the next call checks that the
 * previous transmission has finished, consumes and discards its
 * result, and releases its storage.
 *
 * Do not combine this interface with the asynchronous TX service.
 */
extern int can_twai_send(const can_frame_t *, int);

/* Number of frames discarded because the receive queue was full. */
extern uint32_t can_twai_rx_dropped(void);

/*
 * Replace the current node and apply the requested configuration.
 * Called only from the CAN/OBD task on CPU1.
 */
extern int can_twai_configure(int, uint32_t, int, uint32_t, uint32_t);

/*
 * Schedule one frame without waiting.
 * Called only from the CAN/OBD task.
 * Returns 0 when accepted, EBUSY while the previous request is outstanding.
 */
extern int can_twai_transmit(const can_frame_t *);

/*
 * Consume the transmission result.
 * Returns EAGAIN while pending, 0 on success, EIO on transmission failure,
 * or ENOENT when no request is outstanding.
 */
extern int can_twai_tx_result(void);

/*
 * Stop and delete the node, discarding the outstanding transmission.
 * On success, initialization and configuration are required before reuse.
 * On failure, TX storage remains reserved.
 */
extern int can_twai_abort(void);

#endif /* CARMON_CAN_TWAI_H */
