/*
	File: azaDelay.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_AZADELAY_H
#define AZAUDIO_AZADELAY_H

#include "../azaDSP.h"
#include "../azaMeters.h"

#ifdef __cplusplus
extern "C" {
#endif



extern const azaDSP azaDelayHeader;

typedef struct azaDelayConfig {
	// effect gain in dB
	float gainWet;
	// dry gain in dB
	float gainDry;
	bool muteWet, muteDry;
	uint8_t _reserved[2]; // Explicitly reserved padding for later.
	// delay time in ms
	float delay_ms;
	// 0 to 1 multiple of output feeding back into input
	float feedback;
	// How much of one channel's signal gets added to a different channel in the range 0 to 1
	float pingpong;
} azaDelayConfig;

typedef struct azaDelayChannelConfig {
	// extra delay time in ms
	float delay_ms;
	uint8_t _reserved[4]; // Explicitly reserved padding for later.
} azaDelayChannelConfig;

typedef struct azaDelayChannelData {
	azaDelayChannelConfig config;
	float *buffer;
	uint32_t delaySamples;
	uint32_t index;
} azaDelayChannelData;

typedef struct azaDelay {
	azaDSP dsp;
	azaDelayConfig config;

	// You can provide a chain of effects to operate on the input (including any feedback), which only affect the wet signal
	azaDSPChain inputEffects;

	azaMeters metersInput;
	azaMeters metersOutput;

	// Combined big buffer that gets split for each channel
	float *buffer;
	uint32_t bufferCap;
	uint8_t _reserved[4]; // Explicitly reserved padding for later.
	azaDelayChannelData channelData[AZA_MAX_CHANNEL_POSITIONS];
} azaDelay;

// initializes azaDelay in existing memory
void azaDelayInit(azaDelay *data, azaDelayConfig config);
// frees any additional memory that the azaDelay may have allocated
void azaDelayDeinit(azaDelay *data);
// Resets state. May be called automatically.
void azaDelayReset(azaDelay *data);
// Resets state for the specified channel range
void azaDelayResetChannels(azaDelay *data, uint32_t firstChannel, uint32_t channelCount);

// Convenience function that allocates and inits an azaDelay for you
// May return NULL indicating an out-of-memory error
azaDelay* azaDelayMake(azaDelayConfig config);
// Frees an azaDelay that was created with azaDelayMake
void azaDelayFree(azaDSP *dsp);

azaDSP* azaDelayMakeDefault();
azaDSP* azaDelayMakeDuplicate(azaDSP *src);
int azaDelayCopyConfig(azaDSP *dst, azaDSP *src);

int azaDelayProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);



void azaDelayDraw(azaDSP *dsp, azagRect bounds);



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZADELAY_H
