/*
	File: timer.c
	Author: Philip Haynes
	The non-platform-dependent implementations.
*/

#include "timer.h"

int64_t azaCPUUsageSlowUpdateInterval_ns = 500000000;

void azaCPUUsageTimingStart(azaCPUUsage *data) {
	data->tsOnlineStart = azaGetTimestamp();
}

void azaCPUUsageTimingEnd(azaCPUUsage *data) {
	int64_t tsEnd = azaGetTimestamp();
	if (!data->tsOfflineStart) {
		data->tsOfflineStart = tsEnd;
		data->tsLastSlowUpdate = tsEnd;
		return;
	}
	int64_t nsOffline = azaGetTimestampDeltaNanoseconds(data->tsOnlineStart - data->tsOfflineStart);
	int64_t nsOnline = azaGetTimestampDeltaNanoseconds(tsEnd - data->tsOnlineStart);
	float cpuPercent = (float)(100.0 * (double)nsOnline / (double)(nsOffline + nsOnline));
	data->nsPerTick = (uint32_t)((data->nsPerTick * data->ticks + nsOnline) / (data->ticks+1));
	data->cpuPercent = (data->cpuPercent * (float)data->ticks + cpuPercent) / (float)(data->ticks+1);
	data->ticks++;
	int64_t nsSinceLastSlowUpdate = azaGetTimestampDeltaNanoseconds(tsEnd - data->tsLastSlowUpdate);
	if (nsSinceLastSlowUpdate >= azaCPUUsageSlowUpdateInterval_ns) {
		data->tsLastSlowUpdate = tsEnd;
		data->cpuPercentSlow = data->cpuPercent;
		data->nsPerTickSlow = data->nsPerTick;
		data->ticks = 0;
	}
	data->tsOfflineStart = tsEnd;
}