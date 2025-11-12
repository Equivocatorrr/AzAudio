/*
	File: AzAudio.h
	Author: Philip Haynes
	Main entry point to using the AzAudio library.
*/

#ifndef AZAUDIO_H
#define AZAUDIO_H

#include "backend/interface.h"
#include "aza_c_std.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const unsigned short azaVersionMajor;
extern const unsigned short azaVersionMinor;
extern const unsigned short azaVersionPatch;
extern const char *azaVersionNote;
extern const char *azaVersionString;

#define AZA_VERSION_FORMAT_STR "%hu.%hu.%hu-%s"
#define AZA_VERSION_ARGS azaVersionMajor, azaVersionMinor, azaVersionPatch, azaVersionNote

typedef enum AzaLogLevel {
	AZA_LOG_LEVEL_NONE=0,
	AZA_LOG_LEVEL_ERROR,
	AZA_LOG_LEVEL_INFO,
	AZA_LOG_LEVEL_TRACE,
} AzaLogLevel;
extern AzaLogLevel azaLogLevel;

// Defaults in case querying the devices doesn't work.

#ifndef AZA_SAMPLERATE_DEFAULT
#define AZA_SAMPLERATE_DEFAULT 48000
#endif

#ifndef AZA_CHANNELS_DEFAULT
#define AZA_CHANNELS_DEFAULT 2
#endif

// Setup / Errors

int azaInit();
void azaDeinit();

void azaLogDefault(AzaLogLevel level, const char* message);

// We use a callback function for all message logging.
// This allows the user to define their own logging output functions
typedef void (*fp_azaLogCallback)(AzaLogLevel level, const char* message);

void azaSetLogCallback(fp_azaLogCallback newLogFunc);

// Does the formatting and calls any callback
void azaLog(AzaLogLevel level, const char *format, ...);

#define AZA_LOG_ERR(...) azaLog(AZA_LOG_LEVEL_ERROR, __VA_ARGS__)
#define AZA_LOG_INFO(...) azaLog(AZA_LOG_LEVEL_INFO, __VA_ARGS__)
#define AZA_LOG_TRACE(...) azaLog(AZA_LOG_LEVEL_TRACE, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_H
