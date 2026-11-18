#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "network_test.h"
#include "tcp.h"
#include "utils.h"
void protocol_test_start(void);
void protocol_test_stop(void);

static atomic_bool connected;
static atomic_uint generation;
static atomic_uint short_sends, busy_sends;
static atomic_bool block_send;
static uint16_t port;

bool wifi_is_connected(void) { return atomic_load(&connected); }
uint32_t wifi_get_generation(void) { return atomic_load(&generation); }

/* Exercise partial sends and temporary backpressure in the production loop. */
ssize_t
tcp_test_send(int fd, const void *data, size_t size, int flags)
{
	static unsigned int calls;

	if (atomic_load(&block_send) || ++calls % 5 == 0) {
		atomic_fetch_add(&busy_sends, 1);
		vTaskDelay(1);
		errno = EAGAIN;
		return -1;
	}
	if (size > 3) {
		atomic_fetch_add(&short_sends, 1);
		size = 3;
	}
	return send(fd, data, size, flags);
}

static int
tcp_test_socket(void)
{
	struct timeval timeout = { .tv_sec = 3 };
	int fd = socket(AF_INET, SOCK_STREAM, 0);

	assert(fd >= 0);
	assert(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout)) == 0);
	assert(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof (timeout)) == 0);
	return fd;
}

static int
tcp_test_connect(void)
{
	struct sockaddr_in address = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
		.sin_port = htons(port),
	};

	for (int i = 0; i < 200; i++) {
		int fd = tcp_test_socket();

		if (connect(fd, (struct sockaddr *)&address, sizeof (address)) == 0)
			return fd;
		close(fd);
		vTaskDelay(20);
	}
	assert(!"TCP server did not start");
	return -1;
}

static void
tcp_test_send_all(int fd, const unsigned char *data, size_t length)
{
	while (length != 0) {
		ssize_t count = send(fd, data, length, 0);

		assert(count > 0);
		data += count;
		length -= (size_t)count;
	}
}

static void
tcp_test_receive(int fd, const unsigned char *expected, size_t length)
{
	unsigned char data[701];

	while (length != 0) {
		size_t size = length < sizeof (data) ? length : sizeof (data);
		ssize_t count = recv(fd, data, size, 0);

		assert(count > 0);
		assert(memcmp(expected, data, (size_t)count) == 0);
		expected += count;
		length -= (size_t)count;
	}
}

static void
tcp_test_closed(int fd)
{
	unsigned char byte;
	ssize_t count = recv(fd, &byte, 1, 0);

	assert(count == 0 || (count < 0 && errno == ECONNRESET));
	close(fd);
}

int
main(void)
{
	struct sockaddr_in address;
	socklen_t address_len = sizeof (address);
	unsigned char data[6000], expected[4000];
	int fd, second;

	signal(SIGPIPE, SIG_IGN);
	/* Reserve an ephemeral test port rather than using the firmware port. */
	fd = tcp_test_socket();
	memset(&address, 0, sizeof (address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	assert(bind(fd, (struct sockaddr *)&address, sizeof (address)) == 0);
	assert(getsockname(fd, (struct sockaddr *)&address, &address_len) == 0);
	port = ntohs(address.sin_port);
	close(fd);

	protocol_test_start();
	assert(tcp_server_init(0) == ESP_ERR_INVALID_ARG);
	atomic_store(&network_test_fail_task, true);
	assert(tcp_server_init(port) == ESP_ERR_NO_MEM);
	assert(tcp_server_init(port) == ESP_OK);
	assert(tcp_server_init(port) == ESP_ERR_INVALID_STATE);
	atomic_store(&connected, true);
	fd = tcp_test_connect();
	for (size_t i = 0; i < sizeof (data); i += 6)
		memcpy(data + i, "<PC00>", 6);
	for (size_t i = 0; i < sizeof (expected); i += 4)
		memcpy(expected + i, "<PA>", 4);
	/* Many concatenated requests cross FIFO wrap boundaries and backpressure. */
	tcp_test_send_all(fd, data, sizeof (data));
	tcp_test_receive(fd, expected, sizeof (expected));
	assert(atomic_load(&busy_sends) > 0 && atomic_load(&short_sends) > 0);
	tcp_test_send_all(fd, (const unsigned char *)"<PC", 3);
	vTaskDelay(30);
	tcp_test_send_all(fd, (const unsigned char *)"00>", 3);
	tcp_test_receive(fd, expected, 4);
	tcp_test_send_all(fd, (const unsigned char *)"<><P><X><Pi000001><Pd>", sizeof ("<><P><X><Pi000001><Pd>") - 1);
	tcp_test_receive(fd, (const unsigned char *)"<MN02><MN02><MN01><PN04><PN04>", sizeof ("<MN02><MN02><MN01><PN04><PN04>") - 1);

	second = tcp_test_connect();
	tcp_test_closed(second);
	tcp_test_send_all(fd, data, 6);
	tcp_test_receive(fd, expected, 4);

	tcp_test_send_all(fd, (const unsigned char *)"<PC01>", 6);
	tcp_test_receive(fd, (const unsigned char *)"<PA4B55>", 8);
	tcp_test_send_all(fd, (const unsigned char *)"<P>", 3);
	tcp_test_receive(fd, (const unsigned char *)"<MN05B615>", 10);
	/* Leave a partially received frame behind before changing client. */
	tcp_test_send_all(fd, (const unsigned char *)"<PC", 3);
	vTaskDelay(30);
	/* Link generation changes even if no poll observes connected == false. */
	atomic_fetch_add(&generation, 2);
	tcp_test_closed(fd);
	fd = tcp_test_connect();
	tcp_test_send_all(fd, data, 6);
	tcp_test_receive(fd, expected, 4);

	/* A blocked reader leaves pending TX bytes; none may reach a new client. */
	atomic_store(&block_send, true);
	tcp_test_send_all(fd, data, sizeof (data));
	vTaskDelay(50);
	atomic_store(&connected, false);
	atomic_fetch_add(&generation, 1);
	tcp_test_closed(fd);
	atomic_fetch_add(&generation, 1);
	atomic_store(&block_send, false);
	atomic_store(&connected, true);
	fd = tcp_test_connect();
	tcp_test_send_all(fd, data, sizeof (data));
	tcp_test_receive(fd, expected, sizeof (expected));
	assert(shutdown(fd, SHUT_WR) == 0);
	tcp_test_closed(fd);

	for (int i = 0; i < 5; i++) {
		fd = tcp_test_connect();
		tcp_test_send_all(fd, data, 6);
		tcp_test_receive(fd, expected, 4);
		assert(shutdown(fd, SHUT_WR) == 0);
		tcp_test_closed(fd);
	}
	protocol_test_stop();
	puts("TCP/RTL PASS: framed commands, FIFO wrap, EAGAIN, malformed frames and reconnect");
	return 0;
}
