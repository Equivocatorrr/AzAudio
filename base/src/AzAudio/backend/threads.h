/*
	File: threads.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_THREADS_H
#define AZAUDIO_THREADS_H

#include <stdint.h>
#include <stdalign.h>
#include <stdbool.h>

// This feels super lame but until I can think of a reasonable alternative that doesn't require heap allocations this is what we got.
#ifdef __unix
// Use this to define a variable or argument with a function pointer type for use with azaThreadLaunch
#define AZA_THREAD_PROC_TYPE(name) void* (*name)(void*)
// Use this to define a procedure for use with an azaThread
#define AZA_THREAD_PROC_DEF(name, userdata) void* name(void *userdata)
#elif defined(WIN32)
// Use this to define a variable or argument with a function pointer type for use with azaThreadLaunch
#define AZA_THREAD_PROC_TYPE(name) unsigned (__stdcall *name)(void*)
// Use this to define a procedure for use with an azaThread
#define AZA_THREAD_PROC_DEF(name, userdata) unsigned __stdcall name(void *userdata)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define AZA_THREAD_ALIGNMENT 8
// This should be the total size in bytes of the actual platform-specific thread struct including any padding
#ifdef __unix
	#define AZA_THREAD_SIZE 8
#elif defined(WIN32)
	#define AZA_THREAD_SIZE 12
#endif

typedef struct azaThread {
	alignas(AZA_THREAD_ALIGNMENT) uint8_t data[AZA_THREAD_SIZE];
} azaThread;

#define AZA_MUTEX_ALIGNMENT 8
// This should be the total size in bytes of the actual platform-specific mutex struct including any padding
#define AZA_MUTEX_SIZE 40

typedef struct azaMutex {
	alignas(AZA_MUTEX_ALIGNMENT) uint8_t data[AZA_MUTEX_SIZE];
} azaMutex;

// returns 0 on success, errno on failure
int azaThreadLaunch(azaThread *thread, AZA_THREAD_PROC_TYPE(proc), void *userdata);

bool azaThreadJoinable(azaThread *thread);

void azaThreadJoin(azaThread *thread);

void azaThreadDetach(azaThread *thread);

void azaThreadSleep(uint32_t milliseconds);

void azaThreadYield();

void azaMutexInit(azaMutex *mutex);

void azaMutexDeinit(azaMutex *mutex);

void azaMutexLock(azaMutex *mutex);

void azaMutexUnlock(azaMutex *mutex);

#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_THREADS_H