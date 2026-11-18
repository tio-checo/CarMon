#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <unistd.h>
ssize_t tcp_test_send(int, const void *, size_t, int);
#define send tcp_test_send
