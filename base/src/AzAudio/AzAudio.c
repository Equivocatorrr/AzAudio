/*
	File: AzAudio.c
	Author: Philip Haynes
*/

#include "AzAudio.h"

#include "error.h"
#include "backend/interface.h"
#include "cpuid.h"
#include "dsp/azaKernel.h"
#include "dsp/utility.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if defined(_MSC_VER)
	// Michaelsoft will not be spared my wrath at the end of days
	#define _CRT_USE_CONFORMING_ANNEX_K_TIME 1
#endif
#include <time.h>

fp_azaLogCallback azaLog = azaLogDefault;

AzaLogLevel azaLogLevel = AZA_LOG_LEVEL_INFO;

azaAllocatorCallbacks azaAllocator = {
	calloc,
	malloc,
	realloc,
	free,
};

void azaInitOscillators();

int azaInit() {
	azaCPUIDInit();
	char levelStr[64];
	char *envStr = getenv("AZAUDIO_LOG_LEVEL");
	if (envStr) {
		size_t levelLen = aza_strcpy(levelStr, envStr, sizeof(levelStr));
		if (levelLen <= sizeof(levelStr)) {
			aza_str_to_lower(levelStr, levelStr, sizeof(levelStr));
			if (strncmp(levelStr, "none", sizeof(levelStr)) == 0) {
				azaLogLevel = AZA_LOG_LEVEL_NONE;
			} else if (strncmp(levelStr, "error", sizeof(levelStr)) == 0) {
				azaLogLevel = AZA_LOG_LEVEL_ERROR;
			} else if (strncmp(levelStr, "info", sizeof(levelStr)) == 0) {
				azaLogLevel = AZA_LOG_LEVEL_INFO;
			} else if (strncmp(levelStr, "trace", sizeof(levelStr)) == 0) {
				azaLogLevel = AZA_LOG_LEVEL_TRACE;
			}
		}
	}
	AZA_LOG_INFO("AzAudio Version: " AZA_VERSION_FORMAT_STR "\n", AZA_VERSION_ARGS);

	int err;
	// A resolution of 128 is 2^7, which gives the LUT a signal-to-noise ratio of 12+12*7 = 96dB
	static const uint32_t kernelResolution = 128;
	for (uint32_t radius = 1; radius <= AZA_KERNEL_DEFAULT_LANCZOS_COUNT; radius++) {
		err = azaKernelMakeLanczos(&azaKernelDefaultLanczos[radius-1], kernelResolution, radius);
		if (err) return err;
	}
	err = azaDSPRegistryInit();
	if (err) return err;
	azaInitOscillators();

	memset(&azaWorldDefault, 0, sizeof(azaWorldDefault));
	azaWorldDefault.orientation.right   = (azaVec3) { 1.0f, 0.0f, 0.0f };
	azaWorldDefault.orientation.up      = (azaVec3) { 0.0f, 1.0f, 0.0f };
	azaWorldDefault.orientation.forward = (azaVec3) { 0.0f, 0.0f, 1.0f };
	azaWorldDefault.speedOfSound = 343.0f;
	return azaBackendInit();
}

void azaDeinit() {
	azaBackendDeinit();
}

void azaLogDefault(AzaLogLevel level, const char* format, ...) {
	if (level > azaLogLevel) return;
	FILE *file = level == AZA_LOG_LEVEL_ERROR ? stderr : stdout;
	char timeStr[64];
	strftime(timeStr, sizeof(timeStr), "%T", localtime(&(time_t){time(NULL)}));
	fprintf(file, "AzAudio[%s] ", timeStr);
	va_list args;
	va_start(args, format);
	vfprintf(file, format, args);
	va_end(args);
}

void azaSetLogCallback(fp_azaLogCallback newLogFunc) {
	if (newLogFunc != NULL) {
		azaLog = newLogFunc;
	} else {
		azaLog = azaLogDefault;
	}
}

static const char *azaErrorStr[] = {
	"AZA_SUCCESS",
	"AZA_ERROR_OUT_OF_MEMORY",
	"AZA_ERROR_BACKEND_UNAVAILABLE",
	"AZA_ERROR_BACKEND_LOAD_ERROR",
	"AZA_ERROR_BACKEND_ERROR",
	"AZA_ERROR_NO_DEVICES_AVAILABLE",
	"AZA_ERROR_NULL_POINTER",
	"AZA_ERROR_INVALID_CHANNEL_COUNT",
	"AZA_ERROR_INVALID_FRAME_COUNT",
	"AZA_ERROR_INVALID_CONFIGURATION",
	"AZA_ERROR_MISMATCHED_CHANNEL_COUNT",
	"AZA_ERROR_MISMATCHED_FRAME_COUNT",
	"AZA_ERROR_MISMATCHED_SAMPLERATE",
	"AZA_ERROR_DSP_INTERFACE_EXPECTED_SINGLE",
	"AZA_ERROR_DSP_INTERFACE_EXPECTED_DUAL",
	"AZA_ERROR_DSP_INTERFACE_NOT_GENERIC",
	"AZA_ERROR_MIXER_ROUTING_CYCLE",
};

const char* azaErrorString(int error, char *buffer, size_t bufferSize) {
	if (0 <= error && error < AZA_ERROR_ONE_AFTER_LAST) {
		return azaErrorStr[error];
	}
	if (buffer && bufferSize) {
		snprintf(buffer, bufferSize, "Unknown Error 0x%x", error);
		return buffer;
	}
	return "No buffer for unknown error code :(";
}