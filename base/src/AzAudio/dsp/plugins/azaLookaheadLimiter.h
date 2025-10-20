/*
	File: azaLookaheadLimiter.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_AZALOOKAHEADLIMITER_H
#define AZAUDIO_AZALOOKAHEADLIMITER_H

#include "../azaDSP.h"
#include "../azaMeters.h"

#ifdef __cplusplus
extern "C" {
#endif



// TODO: Work on making the lookahead limiter more transparent without necessarily increasing latency. Perhaps find a way to make our linear envelope more like an S-curve? Also probably make the lookahead time configurable.
// 128 samples at 48.0kHz is 128.0/48.0=2.7ms
// 128 samples at 44.1kHz is 128.0/44.1=2.9ms
//  64 samples at 48.0kHz is  64.0/48.0=1.3ms
//  64 samples at 44.1kHz is  64.0/44.1=1.5ms
#define AZAUDIO_LOOKAHEAD_SAMPLES 128



typedef struct azaLookaheadLimiterConfig {
	// input gain in dB
	float gainInput;
	// output gain in dB
	float gainOutput;
} azaLookaheadLimiterConfig;

typedef struct azaLookaheadLimiterChannelData {
	float valBuffer[AZAUDIO_LOOKAHEAD_SAMPLES];
} azaLookaheadLimiterChannelData;

// NOTE: This limiter increases latency by AZAUDIO_LOOKAHEAD_SAMPLES samples
typedef struct azaLookaheadLimiter {
	azaDSP header;
	azaLookaheadLimiterConfig config;

	azaMeters metersInput;
	azaMeters metersOutput;
	float minAmp, minAmpShort;

	// Data shared by all channels
	float peakBuffer[AZAUDIO_LOOKAHEAD_SAMPLES];
	int index;
	int cooldown;
	float sum;
	float slope;
	azaLookaheadLimiterChannelData channelData[AZA_MAX_CHANNEL_POSITIONS];
} azaLookaheadLimiter;

// initializes azaLookaheadLimiter in existing memory
void azaLookaheadLimiterInit(azaLookaheadLimiter *data, azaLookaheadLimiterConfig config);
// frees any additional memory that the azaLookaheadLimiter may have allocated
void azaLookaheadLimiterDeinit(azaLookaheadLimiter *data);
// Resets our state. May be called automatically.
void azaLookaheadLimiterReset(azaLookaheadLimiter *data);
// Resets state for the specified channel range
void azaLookaheadLimiterResetChannels(azaLookaheadLimiter *data, uint32_t firstChannel, uint32_t channelCount);

// Convenience function that allocates and inits an azaLookaheadLimiter for you
// May return NULL indicating an out-of-memory error
azaLookaheadLimiter* azaMakeLookaheadLimiter(azaLookaheadLimiterConfig config);
// Frees an azaLookaheadLimiter that was created with azaMakeLookaheadLimiter
void azaFreeLookaheadLimiter(void *dsp);

azaDSP* azaMakeDefaultLookaheadLimiter();

int azaLookaheadLimiterProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);

azaDSPSpecs azaLookaheadLimiterGetSpecs(void *dsp, uint32_t samplerate);



static const azaDSP azaLookaheadLimiterHeader = {
	/* .size         = */ sizeof(azaLookaheadLimiter),
	/* .version      = */ 1,
	/* .owned, bypass, selected, prevChannelCountDst, prevChannelCountSrc */ false, false, false, 0, 0,
	/* ._reserved    = */ {0},
	/* .name         = */ "Lookahead Limiter",
	/* fp_getSpecs   = */ azaLookaheadLimiterGetSpecs,
	/* fp_process    = */ azaLookaheadLimiterProcess,
	/* fp_free       = */ azaFreeLookaheadLimiter,
	NULL, NULL,
};



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZALOOKAHEADLIMITER_H
