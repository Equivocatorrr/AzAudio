/*
	File: threads.c
	Author: Philip Haynes
*/

#include "../threads.h"

#include <pthread.h>

#include <assert.h>
#include <stdint.h>
#include <errno.h>


typedef struct azaThread_Linux {
	pthread_t threadHandle;
} azaThread_Linux;
static_assert(alignof(azaThread_Linux) == alignof(azaThread), "Incorrect alignment for azaThread on Linux");
static_assert(sizeof(azaThread_Linux) == sizeof(azaThread), "Incorrect size for azaThread on Linux");

// returns 0 on success, errno on failure
int azaThreadLaunch(azaThread *thread, AZA_THREAD_PROC_TYPE(proc), void *userdata) {
	azaThread_Linux *thread_linux = (azaThread_Linux*)thread;
	int err = pthread_create(&thread_linux->threadHandle, NULL, proc, userdata);
	if (err) {
		thread_linux->threadHandle = 0;
	}
	return err;
}

bool azaThreadJoinable(azaThread *thread) {
	azaThread_Linux *thread_linux = (azaThread_Linux*)thread;
	return thread_linux->threadHandle != 0;
}

void azaThreadJoin(azaThread *thread) {
	azaThread_Linux *thread_linux = (azaThread_Linux*)thread;
	assert(thread_linux->threadHandle != 0);
	pthread_join(thread_linux->threadHandle, NULL);
	thread_linux->threadHandle = 0;
}

void azaThreadDetach(azaThread *thread) {
	azaThread_Linux *thread_linux = (azaThread_Linux*)thread;
	assert(azaThreadJoinable(thread));
	pthread_detach(thread_linux->threadHandle);
	thread_linux->threadHandle = 0;
}

void azaThreadSleep(uint32_t milliseconds) {
	struct timespec remaining = {
		(time_t)(milliseconds / 1000),
		(long)(1000000 * (long)(milliseconds % 1000))
	};
	while (nanosleep(&remaining, &remaining) == -1 && errno == EINTR) {}
}

void azaThreadYield() {
	sched_yield();
}

typedef struct azaMutex_Linux {
	pthread_mutex_t mutex;
} azaMutex_Linux;
static_assert(alignof(azaMutex_Linux) == alignof(azaMutex), "Incorrect alignment for azaMutex on Linux");
static_assert(sizeof(azaMutex_Linux) == sizeof(azaMutex), "Incorrect size for azaMutex on Linux");

void azaMutexInit(azaMutex *mutex) {
	azaMutex_Linux *mutex_linux = (azaMutex_Linux*)mutex;
	pthread_mutexattr_t attr;
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&mutex_linux->mutex, &attr);
	pthread_mutexattr_destroy(&attr);
}

void azaMutexDeinit(azaMutex *mutex) {
	azaMutex_Linux *mutex_linux = (azaMutex_Linux*)mutex;
	pthread_mutex_destroy(&mutex_linux->mutex);
}

void azaMutexLock(azaMutex *mutex) {
	azaMutex_Linux *mutex_linux = (azaMutex_Linux*)mutex;
	pthread_mutex_lock(&mutex_linux->mutex);
}

void azaMutexUnlock(azaMutex *mutex) {
	azaMutex_Linux *mutex_linux = (azaMutex_Linux*)mutex;
	pthread_mutex_unlock(&mutex_linux->mutex);
}
