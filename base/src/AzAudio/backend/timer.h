/*
	File: timer.h
	Author: Philip Haynes
	A high resolution timer for profiling.
*/

#ifndef AZAUDIO_TIMER_H
#define AZAUDIO_TIMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Get a system-specific timestamp. Use azaGetNanoseconds to turn a delta into a useable measurement.
int64_t azaGetTimestamp();

// Converts the difference between two timestamps into Nanoseconds
int64_t azaGetTimestampDeltaNanoseconds(int64_t delta);

#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_TIMER_H