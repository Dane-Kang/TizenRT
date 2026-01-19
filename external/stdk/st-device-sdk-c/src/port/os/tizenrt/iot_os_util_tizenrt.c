/* ***************************************************************************
 *
 * Copyright 2019-2020 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/
#include <tinyara/config.h>
#include <tinyara/version.h>
#include <tinyara/clock.h>

#include <sys/types.h>
#include <sys/statfs.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <string.h>
#include <sched.h>
#include <mqueue.h>
#include <queue_api.h>
#include <semaphore.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

#include "signal.h"
#include "iot_error.h"
#include "iot_debug.h"
#include "iot_os_util.h"

#define pdFALSE 0
#define pdTRUE 1

#define portMAX_DELAY (0xffffffff)

const unsigned int iot_os_max_delay = portMAX_DELAY;
const unsigned int iot_os_true = pdTRUE;
const unsigned int iot_os_false = pdFALSE;

#define VALIDATE_MSEC2TICK(ms) (((ms) == iot_os_max_delay) ? iot_os_max_delay : MSEC2TICK(ms))

typedef struct tizenrt_timer {
	clock_t ticks_to_wait;
	clock_t time_out;
} tizenrt_timer;

/* Thread */
int iot_os_thread_create(void *thread_function, const char *name, int stack_size,
		void *data, int priority, iot_os_thread *thread_handle)
{
	int status;
	pthread_attr_t attr;
	pthread_t pid_h;

	status = pthread_attr_init(&attr);
	if (status != 0) {
		return IOT_OS_FALSE;
	}
	usleep(1000);
	status = pthread_attr_setstacksize(&attr, stack_size);
	if (status != 0) {
		return IOT_OS_FALSE;
	}

	usleep(1000);
	status = pthread_create(&pid_h, &attr, thread_function, (pthread_addr_t)data);
	if (status != 0) {
		free(pid_h);
		return IOT_OS_FALSE;
	}
printf("\n pthread_create done");
usleep(1000);
	pthread_setname_np(pid_h, name);
printf("\n pthread_setname_np done");
usleep(1000);
	if (thread_handle) {
		*thread_handle = (iot_os_thread)pid_h;
	}
	return IOT_OS_TRUE;
}

void iot_os_thread_delete(iot_os_thread thread_handle)
{
	if (thread_handle) {
		pthread_t *pid_h = (pthread_t *)thread_handle;
		pthread_cancel(*pid_h);
		free(pid_h);
	} else {
		pthread_cancel(pthread_self());
	}
}

void iot_os_thread_yield()
{
	sched_yield();
}

int iot_os_thread_get_current_handle(iot_os_thread* thread_handle)
{
    if (thread_handle == NULL) {
        return IOT_OS_FALSE;
    }

    *thread_handle = (iot_os_thread)pthread_self();
    return IOT_OS_TRUE;
}

/* Event Group */
#define EVENT_MAX 8

typedef struct {
	unsigned char id;
	int fd[2];
} event_t;

typedef struct {
	event_t group[EVENT_MAX];
	unsigned char event_status;
	pthread_mutex_t mutex;
} eventgroup_t;

iot_os_eventgroup* iot_os_eventgroup_create(void)
{
	eventgroup_t *eventgroup = malloc(sizeof(eventgroup_t));
	if (eventgroup == NULL)
		return NULL;

	if (pthread_mutex_init(&eventgroup->mutex, NULL)) {
		free(eventgroup);
		return NULL;
	}

	for (int i = 0; i < EVENT_MAX; i++) {
		eventgroup->group[i].id = (1 << i);
		int ret = pipe(eventgroup->group[i].fd);
		if (ret == -1) {
			free(eventgroup);
			return NULL;
		}
	}

	eventgroup->event_status = 0;

	return eventgroup;
}

void iot_os_eventgroup_delete(iot_os_eventgroup* eventgroup_handle)
{
	eventgroup_t* eventgroup = eventgroup_handle;

	for (int i = 0; i < EVENT_MAX; i++) {
		close(eventgroup->group[i].fd[0]);
		close(eventgroup->group[i].fd[1]);
	}
	pthread_mutex_destroy(&eventgroup->mutex);
	free(eventgroup);
}

unsigned char iot_os_eventgroup_wait_bits(iot_os_eventgroup* eventgroup_handle,
		const unsigned char bits_to_wait_for, const int clear_on_exit, const unsigned int wait_time_ms)
{
	eventgroup_t *eventgroup = eventgroup_handle;
	fd_set readfds;
	int fd_max = 0;
	unsigned char event_status_backup;

	FD_ZERO(&readfds);

	for (int i = 0; i < EVENT_MAX; i++) {
		if (eventgroup->group[i].id == (eventgroup->group[i].id & bits_to_wait_for)) {
			FD_SET(eventgroup->group[i].fd[0], &readfds);
			if (eventgroup->group[i].fd[0] >= fd_max) {
				fd_max = eventgroup->group[i].fd[0];
			}
		}
	}

	char buf[3] = {0,};
	struct timeval tv;
	memset(&tv, 0x00, sizeof(tv));
	unsigned char bits = 0x00;
	ssize_t read_size = 0;

	tv.tv_sec = wait_time_ms / 1000;
	tv.tv_usec = (wait_time_ms % 1000) * 1000;

	int ret = select(fd_max + 1, &readfds, NULL, NULL, &tv);
	pthread_mutex_lock(&eventgroup->mutex);
	if (ret == -1) {
		// Select Error
		pthread_mutex_unlock(&eventgroup->mutex);
		return 0;
	} else if (ret == 0) {
		// Select Timeout
		pthread_mutex_unlock(&eventgroup->mutex);
		return (unsigned int)eventgroup->event_status;
	} else {
		// read pipe
		for (int i = 0; i < EVENT_MAX; i++) {
			if (eventgroup->group[i].id == (eventgroup->group[i].id & bits_to_wait_for)) {
				if (FD_ISSET(eventgroup->group[i].fd[0], &readfds)) {
					memset(buf, 0, sizeof(buf));
					read_size = read(eventgroup->group[i].fd[0], buf, sizeof(buf));
					IOT_DEBUG("read_size = %d (%d)", read_size, i);
					bits |= eventgroup->group[i].id;
				}
			}
		}

		event_status_backup = eventgroup->event_status;
		if (clear_on_exit) {
			eventgroup->event_status &= ~(bits);
		}
		pthread_mutex_unlock(&eventgroup->mutex);

		return (unsigned int)event_status_backup;
	}
}

int iot_os_eventgroup_set_bits(iot_os_eventgroup* eventgroup_handle,
		const unsigned char bits_to_set)
{
	eventgroup_t *eventgroup = eventgroup_handle;
	unsigned char bits = 0;
	ssize_t write_size = 0;

	pthread_mutex_lock(&eventgroup->mutex);
	for (int i = 0; i < EVENT_MAX; i++) {
        if (eventgroup->group[i].id == (eventgroup->group[i].id & eventgroup->event_status)) {
            IOT_DEBUG("already set 0x%08x (%d)", eventgroup->group[i].id, i);
            continue;
        }
		if (eventgroup->group[i].id == (eventgroup->group[i].id & bits_to_set)) {
			write_size = write(eventgroup->group[i].fd[1], "Set", strlen("Set"));
			IOT_DEBUG("write_size = %d (%d)", write_size, i);
			bits |= eventgroup->group[i].id;
		}
	}

	eventgroup->event_status |= bits;
	pthread_mutex_unlock(&eventgroup->mutex);

	return IOT_OS_TRUE;
}

int iot_os_eventgroup_clear_bits(iot_os_eventgroup* eventgroup_handle,
		const unsigned char bits_to_clear)
{
    eventgroup_t *eventgroup = eventgroup_handle;

	pthread_mutex_lock(&eventgroup->mutex);
    eventgroup->event_status &= ~(bits_to_clear);
    // TODO: clear written event to pipe
	pthread_mutex_unlock(&eventgroup->mutex);

	return IOT_OS_TRUE;
}

/* Mutex */
static void *recursive_mutex_create_wrapper(void)
{
	pthread_mutexattr_t mattr;
	int status = 0;
	//pthread_mutex_t *mutex = NULL;

	pthread_mutexattr_init(&mattr);
	status = pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_RECURSIVE);
	if (status != 0) {
		return NULL;
	}

	pthread_mutex_t *mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
	if (mutex == NULL) {
		return NULL;
	}

	status = pthread_mutex_init(mutex, &mattr);
	if (status) {
		free(mutex);
		return NULL;
	}
	return (void *)mutex;
}

const char* iot_os_get_os_name()
{
       return "TizenRT";
}

const char* iot_os_get_os_version_string()
{
#ifdef CONFIG_VERSION_STRING
       return CONFIG_VERSION_STRING;
#else
       return "";
#endif
}


int iot_os_mutex_init(iot_os_mutex *mutex)
{
    if (!mutex) {
        return IOT_OS_FALSE;
    }

    mutex->sem = recursive_mutex_create_wrapper();
    if (!mutex->sem) {
        return IOT_OS_FALSE;
    }
    return IOT_OS_TRUE;
}

int iot_os_mutex_lock(iot_os_mutex *mutex)
{
    int ret;

    if (!mutex || !mutex->sem) {
        return IOT_OS_FALSE;
    }

    ret = pthread_mutex_lock((pthread_mutex_t *)mutex->sem);
    if (!ret) {
        ret = IOT_OS_TRUE;
    } else {
        ret = IOT_OS_FALSE;
    }

    return ret;
}

int iot_os_mutex_unlock(iot_os_mutex *mutex)
{
    int ret;

    if (!mutex) {
        return IOT_OS_FALSE;
    }

    ret = pthread_mutex_unlock((pthread_mutex_t *)mutex->sem);
    if (!ret) {
        ret = IOT_OS_TRUE;
    } else {
        ret = IOT_OS_FALSE;
    }

    return ret;
}

void iot_os_mutex_destroy(iot_os_mutex* mutex)
{
    if (!mutex || !mutex->sem)
        return;

    pthread_mutex_destroy((pthread_mutex_t *)mutex->sem);
    free(mutex->sem);
}

/* Delay */
void iot_os_delay(unsigned int delay_ms)
{
	usleep(delay_ms * 1000);
}

static int check_for_timeout(clock_t *const ptime_out, clock_t *const pticks_to_wait)
{
	int ret;

	/* Minor optimisation.  The tick count cannot change in this block. */
	const clock_t const_tick_count = clock_systimer();
	const clock_t elapsed_time = const_tick_count - *ptime_out;

	if (*pticks_to_wait == portMAX_DELAY) {
		/*
		 * If INCLUDE_vTaskSuspend is set to 1 and the block time
		 * specified is the maximum block time then the task should block
		 * indefinitely, and therefore never time out
		 */
		ret = pdFALSE;
	} else if (elapsed_time < *pticks_to_wait ) {
		/* Not a genuine timeout. Adjust parameters for time remaining. */
		*pticks_to_wait -= elapsed_time;
		*ptime_out = clock_systimer();
		ret = pdFALSE;
	} else {
		*pticks_to_wait = 0;
		ret = pdTRUE;
	}

	return ret;
}

void iot_os_timer_count_ms(iot_os_timer timer, unsigned int timeout_ms)
{
	((tizenrt_timer *)timer)->ticks_to_wait = VALIDATE_MSEC2TICK(timeout_ms); /* convert milliseconds to ticks */
	((tizenrt_timer *)timer)->time_out = clock_systimer(); /* Record the time at which this function was entered. */
}

unsigned int iot_os_timer_left_ms(iot_os_timer timer)
{
	tizenrt_timer *os_timer = (tizenrt_timer *)timer;

	if (os_timer->ticks_to_wait == portMAX_DELAY) {
		return portMAX_DELAY;
	}

	check_for_timeout(&os_timer->time_out, &os_timer->ticks_to_wait); /* updates ticks_to_wait to the number left */
	return (os_timer->ticks_to_wait <= 0) ? 0 : TICK2MSEC(os_timer->ticks_to_wait);
}

char iot_os_timer_isexpired(iot_os_timer timer)
{
	tizenrt_timer *os_timer = (tizenrt_timer *)timer;

	return check_for_timeout(&os_timer->time_out, &os_timer->ticks_to_wait) == pdTRUE;
}

int iot_os_timer_init(iot_os_timer *timer)
{
	*timer = malloc(sizeof(tizenrt_timer));
	if (*timer == NULL) {
		return IOT_ERROR_MEM_ALLOC;
	}
	memset(*timer, '\0', sizeof(tizenrt_timer));

	return IOT_ERROR_NONE;
}

void iot_os_timer_destroy(iot_os_timer *timer)
{
	if (timer == NULL || *timer == NULL) {
		return;
	}
	free(*timer);
	*timer = NULL;
}

typedef struct _tizenrt_timer_handle {
	timer_t timerId;
	bool is_started;
	iot_os_timer_cb user_cb;
	void *user_data;
	unsigned int expiry_time_ms;
} _tizenrt_timer_handle;

// static void _port_timer_cb(union sigval timer_data)
// {
// 	posix_timer_handle_t *timer_handle = timer_data.sival_ptr;
// 	timer_handle->is_started = false;

// 	if (timer_handle->user_cb) {
// 		timer_handle->user_cb((iot_os_timer_handle)timer_handle, timer_handle->user_data);
// 	}
// }

iot_os_timer_handle iot_os_timer_create(iot_os_timer_cb cb, unsigned int expiry_time_ms, void *user_data)
{
	_tizenrt_timer_handle *new_timer_handle;
	int res;
	struct sigevent sev = { 0 };

	new_timer_handle = (_tizenrt_timer_handle *)malloc(sizeof(_tizenrt_timer_handle));
	if (new_timer_handle == NULL) {
		return NULL;
	}
	memset(new_timer_handle, 0, sizeof(_tizenrt_timer_handle));

    sev.sigev_notify = SIGEV_SIGNAL;
	//sev.sigev_notify_function = &_port_timer_cb;
    sev.sigev_value.sival_ptr = new_timer_handle;

	res = timer_create(CLOCK_REALTIME, &sev, &new_timer_handle->timerId);
	if (res != 0) {
		free(new_timer_handle);
		return NULL;
	}

	new_timer_handle->user_cb = cb;
	new_timer_handle->user_data = user_data;
	new_timer_handle->is_started = false;
	new_timer_handle->expiry_time_ms = expiry_time_ms;

	return (iot_os_timer_handle)new_timer_handle;
}

void iot_os_timer_delete(iot_os_timer_handle timer_handle)
{
	int err;
	_tizenrt_timer_handle *port_timer_handle = (_tizenrt_timer_handle *)timer_handle;

	err = timer_delete(port_timer_handle->timerId);
	if (err != 0) {
		printf("Failed to delete timer\n");
	}
	free(port_timer_handle);
}

int iot_os_timer_start(iot_os_timer_handle timer_handle)
{
	_tizenrt_timer_handle *port_timer_handle = (_tizenrt_timer_handle *)timer_handle;
    struct itimerspec its = { 0,};
	int err;

	its.it_value.tv_sec = port_timer_handle->expiry_time_ms / 1000;
	its.it_value.tv_nsec = (port_timer_handle->expiry_time_ms % 1000) * 1000;

	err = timer_settime(port_timer_handle->timerId, 0, &its, NULL);
	if (err != 0) {
		printf("Failed to start timer\n");
	} else {
		port_timer_handle->is_started = true;
	}
	return err;
}

int iot_os_timer_stop(iot_os_timer_handle timer_handle)
{
	_tizenrt_timer_handle *port_timer_handle = (_tizenrt_timer_handle *)timer_handle;
    struct itimerspec its = { 0,};
	int err;

	err = timer_settime(port_timer_handle->timerId, 0, &its, NULL);
	if (err != 0) {
		printf("Failed to stop timer\n");
	} else {
		port_timer_handle->is_started = false;
	}
	return err;
}

bool iot_os_timer_is_active(iot_os_timer_handle timer_handle)
{
	_tizenrt_timer_handle *port_timer_handle = (_tizenrt_timer_handle *)timer_handle;
	return port_timer_handle->is_started;
}

iot_error_t iot_bsp_wifi_get_status(void)
{
	return IOT_ERROR_NONE;
}