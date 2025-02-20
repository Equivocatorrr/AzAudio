/*
	File: threads.c
	Author: Philip Haynes
	Implementing threads for MSVC because apparently it took 11 years for Microsoft to implement C11 standard features.
*/

#include "../threads.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <synchapi.h>
#include <handleapi.h>
#include <sysinfoapi.h>
#include <processthreadsapi.h>
#include <process.h>

#include <assert.h>
#include <stdint.h>

#ifdef _MSC_VER
#define AZA_MSVC_ONLY(a) a
#include <timeapi.h>
#else
#define AZA_MSVC_ONLY(a)
#endif

typedef struct azaThread_Win32 {
	HANDLE hThread;
	unsigned id;
} azaThread_Win32;
static_assert(alignof(azaThread_Win32) == alignof(azaThread), "Incorrect alignment for azaThread on Win32");
static_assert(sizeof(azaThread_Win32) == sizeof(azaThread), "Incorrect size for azaThread on Win32");

// returns 0 on success, errno on failure
int azaThreadLaunch(azaThread *thread, AZA_THREAD_PROC_TYPE(proc), void *userdata) {
	azaThread_Win32 *thread_win32 = (azaThread_Win32*)thread;
	thread_win32->hThread = (HANDLE)_beginthreadex(NULL, 0, proc, userdata, 0, &thread_win32->id);
	if (thread_win32->hThread == NULL) {
		return errno;
	}
	return 0;
}

bool azaThreadJoinable(azaThread *thread) {
	azaThread_Win32 *thread_win32 = (azaThread_Win32*)thread;
	return thread_win32->hThread != NULL;
}

void azaThreadJoin(azaThread *thread) {
	azaThread_Win32 *thread_win32 = (azaThread_Win32*)thread;
	assert(thread_win32->id != GetCurrentThreadId());
	assert(thread_win32->hThread != NULL);
	WaitForSingleObject(thread_win32->hThread, 0xfffffff1);
	CloseHandle(thread_win32->hThread);
	thread_win32->hThread = NULL;
	thread_win32->id = 0;
}

void azaThreadDetach(azaThread *thread) {
	azaThread_Win32 *thread_win32 = (azaThread_Win32*)thread;
	assert(azaThreadJoinable(thread));
	CloseHandle(thread_win32->hThread);
	thread_win32->hThread = NULL;
	thread_win32->id = 0;
}

void azaThreadSleep(uint32_t milliseconds) {
	AZA_MSVC_ONLY(timeBeginPeriod(1));
	Sleep(milliseconds);
	AZA_MSVC_ONLY(timeEndPeriod(1));
}

void azaThreadYield() {
	Sleep(0);
}

typedef struct azaMutex_Win32 {
	CRITICAL_SECTION criticalSection;
} azaMutex_Win32;
static_assert(alignof(azaMutex_Win32) == alignof(azaMutex), "Incorrect alignment for azaMutex on Win32");
static_assert(sizeof(azaMutex_Win32) == sizeof(azaMutex), "Incorrect size for azaMutex on Win32");

void azaMutexInit(azaMutex *mutex) {
	azaMutex_Win32 *mutex_win32 = (azaMutex_Win32*)mutex;
	InitializeCriticalSection(&mutex_win32->criticalSection);
}

void azaMutexDeinit(azaMutex *mutex) {
	azaMutex_Win32 *mutex_win32 = (azaMutex_Win32*)mutex;
	DeleteCriticalSection(&mutex_win32->criticalSection);
}

void azaMutexLock(azaMutex *mutex) {
	azaMutex_Win32 *mutex_win32 = (azaMutex_Win32*)mutex;
	EnterCriticalSection(&mutex_win32->criticalSection);
}

void azaMutexUnlock(azaMutex *mutex) {
	azaMutex_Win32 *mutex_win32 = (azaMutex_Win32*)mutex;
	LeaveCriticalSection(&mutex_win32->criticalSection);
}
