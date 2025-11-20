/*
	File: backtrace.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_BACKTRACE_H
#define AZAUDIO_BACKTRACE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif



#define AZA_BACKTRACE_MAX_FRAMES 32
#define AZA_BACKTRACE_MAX_FUNCTION_NAME_LEN 128

typedef struct azaBacktraceFrame {
	char functionName[AZA_BACKTRACE_MAX_FUNCTION_NAME_LEN];
} azaBacktraceFrame;

typedef struct azaBacktrace {
	azaBacktraceFrame frames[AZA_BACKTRACE_MAX_FRAMES];
	size_t count;
} azaBacktrace;

void azaBacktraceInit();
void azaBacktraceDeinit();

// Skips skipFrames frames, allowing you to ignore the top few calls
// Already excludes the call to azaBacktraceGet
azaBacktrace azaBacktraceGet(int skipFrames);


#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_BACKTRACE_H
