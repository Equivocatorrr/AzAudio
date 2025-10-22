/*
	File: azaDelayDynamic.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_AZADELAYDYNAMIC_H
#define AZAUDIO_AZADELAYDYNAMIC_H

#include "../azaDSP.h"
#include "../azaKernel.h"
#include "../azaMeters.h"
#include "../utility.h" // azaFollowerLinear

#ifdef __cplusplus
extern "C" {
#endif



typedef struct azaDelayDynamicChannelConfig {
	// delay in ms, target for the follower
	float delay_ms;
} azaDelayDynamicChannelConfig;

typedef struct azaDelayDynamicConfig {
	// effect gain in dB
	float gainWet;
	// dry gain in dB
	float gainDry;
	bool muteWet, muteDry;
	byte _reserved[6];
	// max possible delay in ms
	// If you increase this it will grow the buffer, filling the empty space with zeroes
	float delayMax_ms;
	// How long it takes to reach the follower target in ms
	float delayFollowTime_ms;
	// 0 to 1 multiple of output feeding back into input
	float feedback;
	// How much of one channel's signal gets added to a different channel in the range 0 to 1
	float pingpong;
	// You can provide a chain of effects to operate on the input (including any feedback), which only affect the wet signal
	azaDSP *inputEffects;
	// Resampling kernel. If NULL it will use azaKernelDefaultLanczos
	azaKernel *kernel;

	azaDelayDynamicChannelConfig channels[AZA_MAX_CHANNEL_POSITIONS];
} azaDelayDynamicConfig;

typedef struct azaDelayDynamicChannelData {
	float *buffer;
	// Keep track of the rate in the previous iteration so we can lerp them and avoid popping caused by changing rates suddenly.
	float ratePrevious;
	// TODO: Timing-sensitive scheduling of follower targets, and probably a spline follower of some kind, since linear delay changes form a non-continuous pitch graph (effectively a sample and hold)
	azaFollowerLinear delay_ms;
} azaDelayDynamicChannelData;

typedef struct azaDelayDynamic {
	azaDSP header;
	azaDelayDynamicConfig config;

	azaMeters metersInput;
	azaMeters metersOutput;

	// Combined big buffer that gets split for each channel
	float *buffer;
	uint32_t bufferCap;
	uint32_t lastSrcBufferFrames;
	azaDelayDynamicChannelData channelData[AZA_MAX_CHANNEL_POSITIONS];
} azaDelayDynamic;

// initializes azaDelayDynamic in existing memory
void azaDelayDynamicInit(azaDelayDynamic *data, azaDelayDynamicConfig config);
// frees any additional memory that the azaDelayDynamic may have allocated
void azaDelayDynamicDeinit(azaDelayDynamic *data);
// Resets state. May be called automatically.
void azaDelayDynamicReset(azaDelayDynamic *data);
// Resets state for the specified channel range
void azaDelayDynamicResetChannels(azaDelayDynamic *data, uint32_t firstChannel, uint32_t channelCount);

// Convenience function that allocates and inits an azaDelayDynamic for you
// May return NULL indicating an out-of-memory error
azaDelayDynamic* azaMakeDelayDynamic(azaDelayDynamicConfig config);
// Frees an azaDelayDynamic that was created with azaMakeDelayDynamic
void azaFreeDelayDynamic(void *dsp);

azaDSP* azaMakeDefaultDelayDynamic();

int azaDelayDynamicProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);

// DelayDynamic's sampling kernel causes there to be a minimum latency requirement, so we'll report that here
azaDSPSpecs azaDelayDynamicGetSpecs(void *dsp, uint32_t samplerate);



static const azaDSP azaDelayDynamicHeader = {
	/* .size         = */ sizeof(azaDelayDynamic),
	/* .version      = */ 1,
	/* .owned, bypass, selected, prevChannelCountDst, prevChannelCountSrc */ false, false, false, 0, 0,
	/* ._reserved    = */ {0},
	/* .error        = */ 0,
	/* .name         = */ "Dynamic Delay",
	/* fp_getSpecs   = */ azaDelayDynamicGetSpecs,
	/* fp_process    = */ azaDelayDynamicProcess,
	/* fp_free       = */ azaFreeDelayDynamic,
	NULL, NULL,
};



// Utilities



// Sets up targets and followers such that it will ramp from start to end perfectly in the span of frames at the given samplerate.
// Useful for inlining into another synchronous process.
// Expects startDelay_ms and endDelay_ms to be in arrays of length numChannels, separated by their associated stride.
void azaDelayDynamicSetRamps(azaDelayDynamic *data, uint8_t numChannels, float startDelay_ms[], float endDelay_ms[], uint32_t frames, uint32_t samplerate);



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZADELAYDYNAMIC_H
