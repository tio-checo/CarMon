#include <errno.h>
#include <stdatomic.h>
#include <string.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "carmon_platform.h"
#include "can_hal.h"
#include "can_twai.h"

#define	CAN_TWAI_RX_QUEUE_SIZE	32

/*
 * Allow one outstanding transmission, including a completed request
 * whose result has not been consumed yet.
 */
enum {
	CAN_TWAI_TX_IDLE = 0,
	CAN_TWAI_TX_BUSY,
	CAN_TWAI_TX_DONE,
	CAN_TWAI_TX_FAILED
};

static atomic_int can_twai_tx_state;
static twai_frame_t can_twai_tx_frame;
static uint8_t can_twai_tx_data[8];

/*
 * Queue storage stays allocated for the lifetime of the application.
 * Queue items contain complete frames, including their data payload.
 */
static StaticQueue_t can_twai_rx_queue_control;
static uint8_t can_twai_rx_queue_storage[
    CAN_TWAI_RX_QUEUE_SIZE * sizeof (carmon_can_frame_t)];
static QueueHandle_t can_twai_rx_queue;
static atomic_uint can_twai_rx_drop_count;

/* Owned by the CAN/OBD task. */
static twai_node_handle_t can_twai_node;
/*
 * A started node must be stopped before it can be deleted.
 * Keep this set if starting or stopping the node fails.
 */
static bool can_twai_stop_required;

/*
 * Copy the received frame into the application queue.
 * Protocol processing is performed later by the CAN/OBD task.
 */
static bool
can_twai_on_rx_done(twai_node_handle_t node,
    const twai_rx_done_event_data_t *event, void *arg)
{
	BaseType_t task_woken = pdFALSE;
	carmon_can_frame_t frame = { 0 };
	twai_frame_t rx = {
		.buffer = frame.data,
		.buffer_len = sizeof (frame.data),
	};

	(void)event;
	(void)arg;

	if (twai_node_receive_from_isr(node, &rx) != ESP_OK) {
		return (false);
	}

	/*
	 * Preserve the logical SID/EID layout expected by the
	 * original CAN and OBD code.
	 */
	frame.msgEID.IDE = rx.header.ide;
	frame.msgEID.RTR = rx.header.rtr;
	frame.msgSID.SID = rx.header.ide ?
	    rx.header.id >> 18 : rx.header.id;
	frame.msgEID.EID = rx.header.ide ?
	    rx.header.id & 0x3ffff : 0;

	/* Classic CAN carries at most eight data bytes. */
	frame.msgEID.DLC = rx.header.dlc > sizeof (frame.data) ?
	    sizeof (frame.data) : rx.header.dlc;

	/*
	 * Software receive timestamp: milliseconds, wrapping at 16 bits
	 * to preserve the original CAN analyzer response format.
	 */
	frame.msgSID.CMSGTS =
	    (uint32_t)(esp_timer_get_time() / 1000) & 0xffff;

	if (xQueueSendFromISR(can_twai_rx_queue, &frame,
	    &task_woken) != pdPASS) {
		(void)atomic_fetch_add_explicit(&can_twai_rx_drop_count,
		    1, memory_order_relaxed);
	}

	return (task_woken == pdTRUE);
}

/*
 * Publish the transmission result to the CAN/OBD task.
 * The frame storage remains reserved until the task consumes the result.
 */
static bool
can_twai_on_tx_done(twai_node_handle_t node,
    const twai_tx_done_event_data_t *event, void *arg)
{
	(void)node;
	(void)arg;

	atomic_store_explicit(&can_twai_tx_state,
	    event->is_tx_success ?
	    CAN_TWAI_TX_DONE : CAN_TWAI_TX_FAILED,
	    memory_order_release);

	return (false);
}

/*
 * Create a stopped controller.
 * Called from the CAN/OBD task on CPU1.
 */
static int
can_twai_create(int mode, uint32_t bitrate)
{
	esp_err_t err;
	twai_onchip_node_config_t config = {
		.io_cfg = {
			.tx = CARMON_GPIO_CAN_TX,
			.rx = CARMON_GPIO_CAN_RX,
			.quanta_clk_out = -1,
			.bus_off_indicator = -1,
		},
		.bit_timing = {
			.bitrate = bitrate,
		},
		.fail_retry_cnt = 0,
		.tx_queue_depth = 1,
		.intr_priority = 1,
		.flags = {
			/*
			 * Receive our own frames without requiring
			 * acknowledgment from another node.
			 */
			.enable_self_test = mode == MODE_LOOPBACK,
			.enable_loopback = mode == MODE_LOOPBACK,
			.enable_listen_only = mode == MODE_LISTEN_ONLY ||
			    mode == MODE_LISTEN_ALL_MESSAGES,
		},
	};
	twai_event_callbacks_t callbacks = {
		.on_tx_done = can_twai_on_tx_done,
		.on_rx_done = can_twai_on_rx_done,
	};

	if (can_twai_node != NULL) {
		return (EBUSY);
	}

	if (can_twai_rx_queue == NULL) {
		can_twai_rx_queue = xQueueCreateStatic(
		    CAN_TWAI_RX_QUEUE_SIZE, sizeof (carmon_can_frame_t),
		    can_twai_rx_queue_storage,
		    &can_twai_rx_queue_control);

		if (can_twai_rx_queue == NULL) {
			return (ENOMEM);
		}
	}

	err = twai_new_node_onchip(&config, &can_twai_node);
	if (err != ESP_OK) {
		ESP_LOGE("can", "Cannot create TWAI node: %s",
		    esp_err_to_name(err));

		return (err == ESP_ERR_NO_MEM ? ENOMEM : EIO);
	}

	err = twai_node_register_event_callbacks(can_twai_node,
	    &callbacks, NULL);
	if (err != ESP_OK) {
		ESP_LOGE("can", "Cannot register TWAI callbacks: %s",
		    esp_err_to_name(err));

		/* The node has not been enabled yet. */
		ESP_ERROR_CHECK(twai_node_delete(can_twai_node));
		can_twai_node = NULL;

		return (EIO);
	}

	return (0);
}

/*
 * Receive a copy of one queued frame without blocking.
 * Called from task context only.
 */
int
can_twai_receive(carmon_can_frame_t *frame)
{
	if (frame == NULL) {
		return (EINVAL);
	}

	if (can_twai_node == NULL || can_twai_rx_queue == NULL) {
		return (ENODEV);
	}

	if (xQueueReceive(can_twai_rx_queue, frame, 0) != pdPASS) {
		return (EAGAIN);
	}

	return (0);
}

/*
 * Read the overflow counter without interfering with the RX callback.
 */
uint32_t
can_twai_rx_dropped(void)
{
	return (atomic_load_explicit(&can_twai_rx_drop_count,
	    memory_order_relaxed));
}

/*
 * Copy and schedule one frame.
 * Both the descriptor and its payload remain valid until completion.
 */
int
can_twai_transmit(const carmon_can_frame_t *frame)
{
	esp_err_t err;
	uint32_t id;

	if (frame == NULL ||
	    frame->msgEID.DLC > sizeof (can_twai_tx_data) ||
	    frame->msgSID.SID > 0x7ff ||
	    (frame->msgEID.IDE && frame->msgEID.EID > 0x3ffff)) {
		return (EINVAL);
	}

	if (can_twai_node == NULL) {
		return (ENODEV);
	}

	/*
	 * This function and can_twai_tx_result() have a single caller:
	 * the CAN/OBD task. Only the completion callback changes the
	 * state asynchronously.
	 */
	if (atomic_load_explicit(&can_twai_tx_state,
	    memory_order_acquire) != CAN_TWAI_TX_IDLE) {
		return (EBUSY);
	}

	id = frame->msgSID.SID;
	if (frame->msgEID.IDE) {
		id = (id << 18) | frame->msgEID.EID;
	}

	can_twai_tx_frame = (twai_frame_t) {
		.header = {
			.id = id,
			.dlc = frame->msgEID.DLC,
			.ide = frame->msgEID.IDE != 0,
			.rtr = frame->msgEID.RTR != 0,
		},
		.buffer = can_twai_tx_data,
		.buffer_len = frame->msgEID.RTR ?
		    0 : frame->msgEID.DLC,
	};

	if (!frame->msgEID.RTR) {
		memcpy(can_twai_tx_data, frame->data,
		    frame->msgEID.DLC);
	}

	/* Completion may occur before twai_node_transmit() returns. */
	atomic_store_explicit(&can_twai_tx_state, CAN_TWAI_TX_BUSY,
	    memory_order_release);

	err = twai_node_transmit(can_twai_node, &can_twai_tx_frame, 0);
	if (err == ESP_OK) {
		return (0);
	}

	/* The request was not accepted; the storage can be reused. */
	atomic_store_explicit(&can_twai_tx_state, CAN_TWAI_TX_IDLE,
	    memory_order_release);

	switch (err) {
	case ESP_ERR_TIMEOUT:
		return (EBUSY);
	case ESP_ERR_INVALID_ARG:
		return (EINVAL);
	case ESP_ERR_NOT_SUPPORTED:
		return (ENOTSUP);
	default:
		return (EIO);
	}
}

/*
 * Consume a completed request and release its storage.
 * Called only from the CAN/OBD task.
 */
int
can_twai_tx_result(void)
{
	int state;

	if (can_twai_node == NULL) {
		return (ENODEV);
	}

	state = atomic_load_explicit(&can_twai_tx_state,
	    memory_order_acquire);

	if (state == CAN_TWAI_TX_IDLE) {
		return (ENOENT);
	}
	if (state == CAN_TWAI_TX_BUSY) {
		return (EAGAIN);
	}

	atomic_store_explicit(&can_twai_tx_state, CAN_TWAI_TX_IDLE,
	    memory_order_release);

	return (state == CAN_TWAI_TX_DONE ? 0 : EIO);
}

/*
 * Release TX storage only after the driver no longer references it.
 * Called from the CAN/OBD task on CPU1.
 */
int
can_twai_abort(void)
{
	esp_err_t err;

	/* Prevent physical transmission while replacing the controller. */
	(void)gpio_set_level(CARMON_GPIO_CAN_SILENT, 1);
	(void)gpio_set_level(CARMON_GPIO_CAN_SHUTDOWN, 1);

	if (can_twai_node == NULL) {
		return (0);
	}

	if (can_twai_stop_required) {
		err = twai_node_disable(can_twai_node);
		if (err != ESP_OK) {
			ESP_LOGE("can", "Cannot stop TWAI node: %s",
			    esp_err_to_name(err));

			return (EIO);
		}

		can_twai_stop_required = false;
	}

	err = twai_node_delete(can_twai_node);
	if (err != ESP_OK) {
		ESP_LOGE("can", "Cannot delete TWAI node: %s",
		    esp_err_to_name(err));

		return (EIO);
	}

	can_twai_node = NULL;

	/* Discard frames belonging to the previous session. */
	(void)xQueueReset(can_twai_rx_queue);

	atomic_store_explicit(&can_twai_tx_state, CAN_TWAI_TX_IDLE,
	    memory_order_release);

	return (0);
}

/*
 * Replace the controller without resubmitting an old transmission.
 */
int
can_twai_configure(int mode, uint32_t bitrate, int filter_mode,
    uint32_t filter, uint32_t mask)
{
	twai_mask_filter_config_t config = { 0 };
	esp_err_t err;
	int ret;

	if (mode < MODE_NORMAL || mode >= CAN_NUM_SUPPORTED_MODES) {
		return (EINVAL);
	}

	if (bitrate != 1000000 && bitrate != 500000 &&
	    bitrate != 250000 && bitrate != 125000) {
		return (EINVAL);
	}

	if (filter_mode < 0 || filter_mode > 3) {
		return (EINVAL);
	}

	/* Mixed SID/EID filtering has not been ported yet. */
	if (mode != MODE_DISABLED &&
	    mode != MODE_LISTEN_ALL_MESSAGES &&
	    !(filter_mode & 1)) {
		return (EINVAL);
	}

	ret = can_twai_abort();
	if (ret != 0) {
		return (ret);
	}

	if (mode == MODE_DISABLED) {
		return (0);
	}

	ret = can_twai_create(mode, bitrate);
	if (ret != 0) {
		return (ret);
	}

	/* Zero ID and mask accept all frames in listen-all mode. */
	if (mode != MODE_LISTEN_ALL_MESSAGES) {
		config.is_ext = (filter_mode & 2) != 0;
		config.mask = mask &
		    (config.is_ext ? 0x1fffffff : 0x7ff);
		config.id = filter &
		    (config.is_ext ? 0x1fffffff : 0x7ff);
	}

	err = twai_node_config_mask_filter(can_twai_node, 0, &config);
	if (err != ESP_OK) {
		goto fail;
	}

	/* Wake the transceiver while its transmitter remains disabled. */
	(void)gpio_set_level(CARMON_GPIO_CAN_SHUTDOWN, 0);
	esp_rom_delay_us(20);

	/*
	 * If enabling fails, require successful stopping before
	 * releasing the node and its TX storage.
	 */
	can_twai_stop_required = true;

	err = twai_node_enable(can_twai_node);
	if (err != ESP_OK) {
		goto fail;
	}

	/*
	 * Enable physical transmission in normal and loopback modes.
	 */
	if (mode == MODE_NORMAL || mode == MODE_LOOPBACK) {
		(void)gpio_set_level(CARMON_GPIO_CAN_SILENT, 0);
		esp_rom_delay_us(20);
	}

	return (0);

fail:
	ESP_LOGE("can", "Cannot configure TWAI node: %s",
	    esp_err_to_name(err));

	(void)can_twai_abort();

	return (EIO);
}

/*
 * Read the information currently supported by the public TWAI API.
 */
int
can_twai_get_status(can_twai_status_t *status)
{
	twai_node_status_t info;
	esp_err_t err;

	if (status == NULL) {
		return (EINVAL);
	}

	memset(status, 0, sizeof (*status));

	if (can_twai_node == NULL) {
		return (ENODEV);
	}

	/* A created but stopped node must not be reported as bus-off. */
	if (!can_twai_stop_required) {
		return (EIO);
	}

	err = twai_node_get_info(can_twai_node, &info, NULL);
	if (err != ESP_OK) {
		return (EIO);
	}

	/*
	 * ESP-IDF 5.5.1 does not update the reported error counters.
	 * Do not present their zero values as valid measurements.
	 */
	status->flags = CAN_TWAI_STATUS_INCOMPLETE;
	status->tx_errors = 0xff;
	status->rx_errors = 0xff;

	switch (info.state) {
	case TWAI_ERROR_ACTIVE:
		break;

	case TWAI_ERROR_WARNING:
	case TWAI_ERROR_PASSIVE:
		status->flags |= CAN_TWAI_STATUS_WARNING;
		break;

	case TWAI_ERROR_BUS_OFF:
		status->flags |= CAN_TWAI_STATUS_WARNING |
		    CAN_TWAI_STATUS_BUS_OFF;
		break;

	default:
		return (EIO);
	}

	if (can_twai_rx_dropped() != 0) {
		status->flags |= CAN_TWAI_STATUS_RX_DROPPED;
	}

	return (0);
}
