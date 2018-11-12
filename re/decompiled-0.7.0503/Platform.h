#pragma once


#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <ctime>


#ifdef _WIN32
	#include <Windows.h>
#else
	#include <unistd.h>
	#include <sys/time.h>
	#include <pthread.h>
	
	#include "WinAPITypes.h"
#endif


#ifndef _WIN32 // ==============================================================

inline void Sleep(DWORD dwMilliseconds)
{
	usleep((useconds_t)dwMilliseconds * 1'000u);
}

#endif // ======================================================================


#ifndef _WIN32 // ==============================================================

inline DWORD GetTickCount()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	
	return ((ts.tv_nsec / 1'000'000) + (ts.tv_sec * 1'000));
}

#endif // ======================================================================


// MOVE ME =====================================================================

#define DbgPrint(fmt, ...) DbgPrint(__FUNCTION__, fmt, ##__VA_ARGS__)

void DbgPrint(const char *func, const char *fmt, ...)
{
	static int time0 = GetTickCount(); // <-- unused variable
	
	extern bool g_bDebugPrint;
	if (g_bDebugPrint) {
		char buf[0x100];
		memset(buf, '\x00', sizeof(buf));
		sprintf(buf, "[%s]: ", func); // <-- possible buffer overrun
		
		(void)GetTickCount(); // <-- unused return value
		
		char *buf_end = buf + strlen(buf);
		
		va_list va;
		va_start(va, fmt);
		vsnprintf(buf_end, sizeof(buf) - (buf_end - buf), fmt, va);
		va_end(va);
		
		fputs(buf, stderr);
	}
}

// =============================================================================


#ifndef _WIN32 // ==============================================================

using   CRITICAL_SECTION = pthread_mutex_t;
using LPCRITICAL_SECTION = pthread_mutex_t *;

inline void InitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
	(void)pthread_mutex_init(lpCriticalSection, nullptr);
}
inline void DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
	(void)pthread_mutex_destroy(lpCriticalSection);
}
inline void EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
	(void)pthread_mutex_lock(lpCriticalSection);
}
inline void LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
	(void)pthread_mutex_unlock(lpCriticalSection);
}

#endif // ======================================================================


#ifdef _WIN32 // ===============================================================

using   CONDITION_VARIABLE =   HANDLE;
using LPCONDITION_VARIABLE = LPHANDLE;

inline void InitializeConditionVariable(LPCONDITION_VARIABLE lpConditionVariable)
{
	*lpConditionVariable = CreateSemaphore(nullptr, 0, 1, nullptr);
}
inline void DeleteConditionVariable(LPCONDITION_VARIABLE lpConditionVariable)
{
	(void)CloseHandle(*lpConditionVariable);
}
inline void SignalConditionVariable(LPCONDITION_VARIABLE lpConditionVariable, LPCRITICAL_SECTION lpCriticalSection)
{
	(void)lpCriticalSection;
	(void)ReleaseSemaphore(*lpConditionVariable, 1, nullptr);
}
inline bool WaitForConditionVariable(LPCONDITION_VARIABLE lpConditionVariable, LPCRITICAL_SECTION lpCriticalSection, DWORD dwMilliseconds)
{
	(void)lpCriticalSection;
	return (WaitForSingleObject(*lpConditionVariable, dwMilliseconds) != WAIT_TIMEOUT);
}

#else // =======================================================================

constexpr DWORD INFINITE = 0xffffffff;

using   CONDITION_VARIABLE = pthread_cond_t;
using LPCONDITION_VARIABLE = pthread_cond_t *;

inline void InitializeConditionVariable(LPCONDITION_VARIABLE lpConditionVariable)
{
	(void)pthread_cond_init(lpConditionVariable, nullptr);
}
inline void DeleteConditionVariable(LPCONDITION_VARIABLE lpConditionVariable)
{
	(void)pthread_cond_destroy(lpConditionVariable);
}
inline void SignalConditionVariable(LPCONDITION_VARIABLE lpConditionVariable, LPCRITICAL_SECTION lpCriticalSection)
{
	(void)pthread_mutex_lock(lpCriticalSection);
	(void)pthread_cond_signal(lpConditionVariable);
	(void)pthread_mutex_unlock(lpCriticalSection); // <-- WARNING! This isn't there in all cases, it seems!
}
inline bool WaitForConditionVariable(LPCONDITION_VARIABLE lpConditionVariable, LPCRITICAL_SECTION lpCriticalSection, DWORD dwMilliseconds)
{
	if (dwMilliseconds == INFINITE) {
		(void)pthread_mutex_lock(lpCriticalSection);
		(void)pthread_cond_wait(lpConditionVariable, lpCriticalSection);
		(void)pthread_mutex_unlock(lpCriticalSection);
		
		return true;
	} else {
		struct timeval tv;
		(void)gettimeofday(&tv, nullptr);
		
		// THIS CODE IS SO IDIOTIC IT MADE MY BRAIN EXPLODE ALL OVER THE PLACE
		struct timespec ts;
		ts.tv_sec  = tv.tv_sec + (dwMilliseconds / 1000) + 1;
		ts.tv_nsec = 0;
		
		(void)pthread_mutex_lock(lpCriticalSection);
		int result = pthread_cond_timedwait(lpConditionVariable, lpCriticalSection, &t);
		(void)pthread_mutex_unlock(lpCriticalSection);
		
		return (result != ETIMEDOUT);
	}
}

#endif // ======================================================================


#ifdef _WIN32 // ===============================================================

using   THREAD =   HANDLE;
using LPTHREAD = LPHANDLE;

inline bool BeginThread(LPTHREAD lpThread, void (__cdecl *lpFunc)(void *), void *arg)
{
	*lpThread = _beginthread(lpFunc, 0, arg);
	
	return (*lpThread != INVALID_HANDLE_VALUE);
}
inline void JoinThread(LPTHREAD lpThread)
{
	(void)WaitForSingleObject(*lpThread);
}

#else // =======================================================================

using   THREAD = pthread_t;
using LPTHREAD = pthread_t *;

inline bool BeginThread(LPTHREAD lpThread, void *(__cdecl *lpFunc)(void *), void *arg)
{
	return (pthread_create(lpThread, nullptr, lpFunc, arg) == 0);
}
inline void JoinThread(LPTHREAD lpThread)
{
	(void)pthread_join(*lpThread, nullptr);
}

#endif // ======================================================================


// NOTE: all of this return-value-ignorance stuff makes me uneasy...
// could it be that the program is ignoring important error codes from pthread?
// perhaps we should shim-hook the pthread calls and print out obvious red debug
// log lines if the return value is an error of any sort
