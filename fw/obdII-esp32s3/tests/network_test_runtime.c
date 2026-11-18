#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "network_test.h"

struct network_test_events {
	pthread_mutex_t lock;
	pthread_cond_t changed;
	EventBits_t bits;
};

struct network_test_task {
	void (*function)(void *);
	void *arg;
};

atomic_bool network_test_fail_task;
static atomic_int_fast64_t network_test_clock_offset;

const char *
esp_err_to_name(esp_err_t err)
{
	return err == ESP_OK ? "ESP_OK" : "test error";
}

EventGroupHandle_t
xEventGroupCreate(void)
{
	EventGroupHandle_t group = calloc(1, sizeof (*group));

	assert(group != NULL);
	assert(pthread_mutex_init(&group->lock, NULL) == 0);
	assert(pthread_cond_init(&group->changed, NULL) == 0);
	return group;
}

EventBits_t
xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits)
{
	EventBits_t result;

	pthread_mutex_lock(&group->lock);
	result = group->bits |= bits;
	pthread_cond_signal(&group->changed);
	pthread_mutex_unlock(&group->lock);
	return result;
}

EventBits_t
xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t mask, int clear,
    int all, TickType_t timeout)
{
	struct timespec end;
	EventBits_t result;

	assert(!all);
	clock_gettime(CLOCK_REALTIME, &end);
	end.tv_sec += timeout / 1000;
	end.tv_nsec += (timeout % 1000) * 1000000L;
	if (end.tv_nsec >= 1000000000L) {
		end.tv_sec++;
		end.tv_nsec -= 1000000000L;
	}
	pthread_mutex_lock(&group->lock);
	while (!(group->bits & mask)) {
		int err = pthread_cond_timedwait(&group->changed, &group->lock, &end);

		if (err == ETIMEDOUT)
			break;
		assert(err == 0);
	}
	result = group->bits;
	if (clear)
		group->bits &= ~mask;
	pthread_mutex_unlock(&group->lock);
	return result;
}

void
vEventGroupDelete(EventGroupHandle_t group)
{
	pthread_mutex_destroy(&group->lock);
	pthread_cond_destroy(&group->changed);
	free(group);
}

static void *
network_test_task_start(void *arg)
{
	struct network_test_task task = *(struct network_test_task *)arg;

	free(arg);
	task.function(task.arg);
	return NULL;
}

int
xTaskCreatePinnedToCore(void (*function)(void *), const char *name,
    uint32_t stack, void *arg, unsigned int priority, void *handle, int core)
{
	struct network_test_task *task;
	pthread_t thread;

	(void)name;
	(void)stack;
	(void)priority;
	(void)handle;
	assert(core == 0);
	if (atomic_exchange(&network_test_fail_task, false))
		return 0;
	task = malloc(sizeof (*task));
	assert(task != NULL);
	task->function = function;
	task->arg = arg;
	assert(pthread_create(&thread, NULL, network_test_task_start, task) == 0);
	pthread_detach(thread);
	return pdPASS;
}

void
vTaskDelay(TickType_t ticks)
{
	struct timespec delay = {
		.tv_sec = ticks / 1000,
		.tv_nsec = (ticks % 1000) * 1000000L,
	};

	nanosleep(&delay, NULL);
}

void
vTaskDelete(void *task)
{
	assert(task == NULL);
	pthread_exit(NULL);
}

int64_t
esp_timer_get_time(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return now.tv_sec * 1000000LL + now.tv_nsec / 1000 +
	    atomic_load(&network_test_clock_offset);
}

void
network_test_advance_time(int64_t us)
{
	atomic_fetch_add(&network_test_clock_offset, us);
}

void
network_test_log(const char *tag, const char *format, ...)
{
	va_list args;

	flockfile(stdout);
	printf("%s: ", tag);
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
	putchar('\n');
	fflush(stdout);
	funlockfile(stdout);
}
