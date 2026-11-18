/*********************************************************************
 *
 * TCP transport for the original RTL master protocol.
 * All sockets belong to tcp_task. No socket API is called from RTL.
 *
 *********************************************************************/

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "tcp.h"
#include "wifi.h"

/* constants */
#define TCP_BUFFER_SIZE		1024
#define TCP_POLL_MS		10

/* local variables */
static const char *tcp_tag = "carmon_tcp";
static bool tcp_started;
static uint16_t tcp_port;

/*
 * Single producer/single consumer queues. RX: TCP -> RTL, TX: RTL -> TCP.
 * Publish bytes with release/acquire ordering across the two CPUs. No locks,
 * allocation or socket calls are needed in the periodic interrupt.
 */
_Static_assert(sizeof (unsigned int) == 4, "FIFO indices must be 32-bit");
struct tcp_fifo {
	unsigned char data[TCP_BUFFER_SIZE];
	atomic_uint head, tail;
};
static struct tcp_fifo tcp_rx, tcp_tx;
static atomic_uint tcp_connected, tcp_session, tcp_ack;

static unsigned int
tcp_fifo_space(struct tcp_fifo *fifo)
{
	return TCP_BUFFER_SIZE - (atomic_load(&fifo->head) -
	    atomic_load(&fifo->tail));
}

static int
tcp_fifo_put(struct tcp_fifo *fifo, unsigned char ch)
{
	unsigned int head = atomic_load_explicit(&fifo->head, memory_order_relaxed);

	if (head - atomic_load_explicit(&fifo->tail, memory_order_acquire) ==
	    TCP_BUFFER_SIZE)
		return EAGAIN;
	fifo->data[head % TCP_BUFFER_SIZE] = ch;
	atomic_store_explicit(&fifo->head, head + 1, memory_order_release);
	return 0;
}

static int
tcp_fifo_get(struct tcp_fifo *fifo, unsigned char *ch)
{
	unsigned int tail = atomic_load_explicit(&fifo->tail, memory_order_relaxed);

	if (tail == atomic_load_explicit(&fifo->head, memory_order_acquire))
		return EAGAIN;
	*ch = fifo->data[tail % TCP_BUFFER_SIZE];
	atomic_store_explicit(&fifo->tail, tail + 1, memory_order_release);
	return 0;
}

/* RTL-facing API: 0 means accepted, EAGAIN means try again next tick. */
int
tcp_getchar(unsigned char *ch)
{
	return tcp_is_connected() ? tcp_fifo_get(&tcp_rx, ch) : EAGAIN;
}

int
tcp_putchar(unsigned char ch, BOOL block)
{
	(void)block; /* Always nonblocking, including calls from an ISR. */
	return tcp_is_connected() ? tcp_fifo_put(&tcp_tx, ch) : 0;
}

BOOL
tcp_is_connected(void)
{
	return atomic_load(&tcp_connected) != 0;
}

BOOL
tcp_session_changed(void)
{
	return atomic_load(&tcp_session) != atomic_load(&tcp_ack);
}

void
tcp_session_ack(void)
{
	atomic_store(&tcp_ack, atomic_load(&tcp_session));
}

/*
 * Called by TCP before exposing a new client. RTL resets its protocol state
 * first; only then may TCP clear both queues while RTL sees disconnected.
 */
static bool
tcp_session_start(void)
{
	atomic_store(&tcp_connected, 0);
	atomic_store(&tcp_session, atomic_load(&tcp_session) + 1);
	for (int i = 0; i < 100; i++) {
		if (!tcp_session_changed()) {
			atomic_store(&tcp_rx.head, 0);
			atomic_store(&tcp_rx.tail, 0);
			atomic_store(&tcp_tx.head, 0);
			atomic_store(&tcp_tx.tail, 0);
			atomic_store(&tcp_connected, 1);
			return true;
		}
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	return false;
}

/* local functions */

/* Make all socket operations nonblocking, including those after select(). */
static int
tcp_set_nonblock(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		return -1;
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* Open a listener for the current Wi-Fi connection. */
static int
tcp_open_listener(void)
{
	struct sockaddr_in address = { 0 };
	int fd;
	int reuse = 1;

	fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd < 0)
		return -1;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(tcp_port);
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof (reuse)) < 0 ||
	    tcp_set_nonblock(fd) < 0 ||
	    bind(fd, (struct sockaddr *)&address, sizeof (address)) < 0 ||
	    listen(fd, 1) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

/*
 * Serve one client at a time. Keep accepting on the listener so a second
 * connection is closed explicitly, rather than left waiting in the backlog.
 *
 * Stop reading the socket when RX FIFO is full. Preserve unsent TX bytes
 * across short writes; neither direction waits inside the RTL interrupt.
 */
static void
tcp_serve(int listener, uint32_t generation)
{
	unsigned char buffer[TCP_BUFFER_SIZE], received[TCP_BUFFER_SIZE];
	size_t used = 0;
	size_t sent = 0;
	int client = -1;

	while (wifi_is_connected() && wifi_get_generation() == generation) {
		struct timeval timeout;
		fd_set read_fds, write_fds;
		int max_fd = listener;
		int result;
		unsigned int space = tcp_fifo_space(&tcp_rx);

		if (client >= 0 && used == 0) {
			while (used < sizeof (buffer) &&
			    tcp_fifo_get(&tcp_tx, &buffer[used]) == 0)
				used++;
		}

		FD_ZERO(&read_fds);
		FD_ZERO(&write_fds);
		FD_SET(listener, &read_fds);
		if (client >= 0) {
			if (space != 0)
				FD_SET(client, &read_fds);
			if (used != 0)
				FD_SET(client, &write_fds);
			if (client > max_fd)
				max_fd = client;
		}
		timeout.tv_sec = 0;
		timeout.tv_usec = TCP_POLL_MS * 1000;
		result = select(max_fd + 1, &read_fds, &write_fds, NULL, &timeout);
		if (result < 0) {
			if (errno == EINTR)
				continue;
			ESP_LOGW(tcp_tag, "Select failed, errno=%d", errno);
			break;
		}

		/* A quick reconnect must not preserve the old client's session. */
		if (!wifi_is_connected() || wifi_get_generation() != generation)
			break;
		if (result == 0)
			continue;

		if (FD_ISSET(listener, &read_fds)) {
			int fd = accept(listener, NULL, NULL);

			if (fd >= 0) {
				int enabled = 1;

				if (client >= 0) {
					ESP_LOGW(tcp_tag, "Rejecting additional client");
					close(fd);
				} else if (tcp_set_nonblock(fd) < 0 ||
				    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
				    &enabled, sizeof (enabled)) < 0 ||
				    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
				    &enabled, sizeof (enabled)) < 0) {
					ESP_LOGW(tcp_tag, "Client setup failed, errno=%d",
					    errno);
					close(fd);
				} else {
					if (!tcp_session_start()) {
						close(fd);
						continue;
					}
					client = fd;
					ESP_LOGI(tcp_tag, "Client connected");
					/* New descriptor was not in this select set. */
					continue;
				}
			} else if (errno != EAGAIN && errno != EWOULDBLOCK &&
			    errno != EINTR) {
				ESP_LOGW(tcp_tag, "Accept failed, errno=%d", errno);
				break;
			}
		}

		if (client >= 0 && FD_ISSET(client, &read_fds)) {
			ssize_t count;

			count = recv(client, received, space, 0);
			if (count > 0) {
				for (ssize_t i = 0; i < count; i++)
					tcp_fifo_put(&tcp_rx, received[i]);
			} else if (count == 0 || (errno != EAGAIN &&
			    errno != EWOULDBLOCK && errno != EINTR)) {
				goto disconnected;
			}
		}

		if (client >= 0 && FD_ISSET(client, &write_fds)) {
			ssize_t count;

			count = send(client, buffer + sent, used - sent, 0);
			if (count > 0) {
				sent += (size_t)count;
				if (sent == used)
					sent = used = 0;
			} else if (count == 0 || (errno != EAGAIN &&
			    errno != EWOULDBLOCK && errno != EINTR)) {
				goto disconnected;
			}
		}
		continue;

disconnected:
		atomic_store(&tcp_connected, 0);
		ESP_LOGI(tcp_tag, "Client disconnected");
		close(client);
		client = -1;
		sent = used = 0;
	}

	atomic_store(&tcp_connected, 0);
	if (client >= 0)
		close(client);
}

/* Wait for an address before opening the server, and recover after link loss. */
static void
tcp_task(void *arg)
{
	(void)arg;

	for (;;) {
		uint32_t generation;
		int listener;

		if (!wifi_is_connected()) {
			vTaskDelay(pdMS_TO_TICKS(TCP_POLL_MS));
			continue;
		}
		generation = wifi_get_generation();
		listener = tcp_open_listener();
		if (listener < 0) {
			ESP_LOGW(tcp_tag, "Listen failed, errno=%d; retrying", errno);
			vTaskDelay(pdMS_TO_TICKS(1000));
			continue;
		}
		ESP_LOGI(tcp_tag, "CarMon server listening on port %u",
		    (unsigned int)tcp_port);
		tcp_serve(listener, generation);
		close(listener);
	}
}

/* public functions */
esp_err_t
tcp_server_init(uint16_t port)
{
	if (port == 0)
		return ESP_ERR_INVALID_ARG;
	if (tcp_started)
		return ESP_ERR_INVALID_STATE;
	tcp_port = port;
	if (xTaskCreatePinnedToCore(tcp_task, "tcp", 6144, NULL, 5,
	    NULL, 0) != pdPASS)
		return ESP_ERR_NO_MEM;
	tcp_started = true;
	return ESP_OK;
}
