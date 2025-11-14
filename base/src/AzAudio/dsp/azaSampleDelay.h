/*
	File: azaSampleDelay.h
	Author: Philip Haynes
	Sample delay utility with no extra bells and whistles. Primarily for facilitating latency compensation.
	This is deliberately not a plugin because azaDelay already does everything in here and more.
*/

#ifndef AZAUDIO_AZASAMPLEDELAY_H
#define AZAUDIO_AZASAMPLEDELAY_H

// #include "../aza_c_std.h"
#include "azaBuffer.h"

#ifdef __cplusplus
extern "C" {
#endif



typedef struct azaSampleDelayConfig {
	uint32_t delayFrames;
} azaSampleDelayConfig;

typedef struct azaSampleDelay {
	azaSampleDelayConfig config;
	azaBuffer buffer;
} azaSampleDelay;
// initializes azaSampleDelay in existing memory
void azaSampleDelayInit(azaSampleDelay *data, azaSampleDelayConfig config);
// frees any additional memory that the azaSampleDelay may have allocated
void azaSampleDelayDeinit(azaSampleDelay *data);
int azaSampleDelayProcess(azaSampleDelay *data, azaBuffer *dst, azaBuffer *src, uint32_t flags);



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZASAMPLEDELAY_H
