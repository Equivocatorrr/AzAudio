/*
	File: timer.h
	Author: Philip Haynes
	A high resolution timer for profiling.
*/

#ifndef AZAUDIO_TIMER_H
#define AZAUDIO_TIMER_H

#include <stdint.h>
#include "math.h"

#ifdef __cplusplus
extern "C" {
#endif

// Get a system-specific timestamp. Use azaGetNanoseconds to turn a delta into a useable measurement.
int64_t azaGetTimestamp();

// Converts the difference between two timestamps into Nanoseconds
int64_t azaGetTimestampDeltaNanoseconds(int64_t delta);

static int64_t aza_ns_from_ms(int64_t milliseconds) {
	return milliseconds * 1000000;
}

static int64_t aza_ns_from_ms_f(float milliseconds) {
	return (int64_t)round((double)milliseconds * 1000000.0);
}

static int64_t aza_ns_from_s_f(float seconds) {
	return (int64_t)round((double)seconds * 1000000000.0);
}

// How many nanoseconds must pass between azaCPUUsage instances updating their slow metrics.
extern int64_t azaCPUUsageSlowUpdateInterval_ns;

typedef struct azaCPUUsage {
	// State

	int64_t tsOnlineStart;
	int64_t tsOfflineStart;
	int64_t tsLastSlowUpdate;
	uint32_t ticks;
	// Output

	float cpuPercent;
	float cpuPercentSlow; // Updated every nsPerSlowUpdate
	uint32_t nsPerTick;
	uint32_t nsPerTickSlow; // Updated every nsPerSlowUpdate

	aza_byte _reserved[4];
} azaCPUUsage;

void azaCPUUsageTimingStart(azaCPUUsage *data);
void azaCPUUsageTimingEnd(azaCPUUsage *data);

#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_TIMER_H