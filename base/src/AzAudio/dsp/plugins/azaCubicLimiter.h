/*
	File: azaCubicLimiter.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_AZACUBICLIMITER_H
#define AZAUDIO_AZACUBICLIMITER_H

#include "../azaDSP.h"
#include "../azaMeters.h"

#ifdef __cplusplus
extern "C" {
#endif



typedef struct azaCubicLimiterConfig {
	// input gain in dB
	float gainInput;
	// output gain in dB
	float gainOutput;
	// if linkGain is true, any increase in gainInput has an equal decrease in gainOutput (only relevant to the mixer GUI)
	bool linkGain;
	uint8_t _reserved[7]; // Explicit padding bytes, reserved for future use.
} azaCubicLimiterConfig;

typedef struct azaCubicLimiter {
	azaDSP header;
	azaCubicLimiterConfig config;

	azaMeters metersInput;
	azaMeters metersOutput;
} azaCubicLimiter;

// initializes azaCubicLimiter in existing memory
void azaCubicLimiterInit(azaCubicLimiter *data, azaCubicLimiterConfig config);
// frees any additional memory that the azaCubicLimiter may have allocated
void azaCubicLimiterDeinit(azaCubicLimiter *data);
// Resets state. May be called automatically.
void azaCubicLimiterReset(azaCubicLimiter *data);
// Resets state for the specified channel range
void azaCubicLimiterResetChannels(azaCubicLimiter *data, uint32_t firstChannel, uint32_t channelCount);

// Convenience function that allocates and inits an azaCubicLimiter for you
// May return NULL indicating an out-of-memory error
azaCubicLimiter* azaMakeCubicLimiter(azaCubicLimiterConfig config);
// Frees an azaCubicLimiter that was created with azaMakeCubicLimiter
void azaFreeCubicLimiter(void *dsp);

azaDSP* azaMakeDefaultCubicLimiter();

int azaCubicLimiterProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);



static const azaDSP azaCubicLimiterHeader = {
	/* .size         = */ sizeof(azaCubicLimiter),
	/* .version      = */ 1,
	/* .owned, bypass, selected, prevChannelCountDst, prevChannelCountSrc */ false, false, false, 0, 0,
	/* ._reserved    = */ {0},
	/* .error        = */ 0,
	/* .name         = */ "Cubic Limiter",
	/* fp_getSpecs   = */ NULL,
	/* fp_process    = */ azaCubicLimiterProcess,
	/* fp_free       = */ azaFreeCubicLimiter,
};



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZACUBICLIMITER_H
