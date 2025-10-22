/*
	File: azaReverb.h
	Author: Philip Haynes
	TODO: This implementation is really bad and horrible and stupid. Please do something smarter, like at the very least a unified buffer for delays. Even better, probably use frequency space.
*/

#ifndef AZAUDIO_AZAREVERB_H
#define AZAUDIO_AZAREVERB_H

#include "../azaDSP.h"
#include "../azaMeters.h"
#include "azaDelay.h"
#include "azaFilter.h"

#ifdef __cplusplus
extern "C" {
#endif



typedef struct azaReverbConfig {
	// effect gain in dB
	float gainWet;
	// dry gain in dB
	float gainDry;
	bool muteWet, muteDry;
	// value affecting reverb feedback, roughly in the range of 1 to 100 for reasonable results
	float roomsize;
	// value affecting damping of high frequencies, roughly in the range of 1 to 5
	float color;
	// delay for first reflections in ms
	float delay_ms;
} azaReverbConfig;

#define AZAUDIO_REVERB_DELAY_COUNT 30
typedef struct azaReverb {
	azaDSP header;
	azaReverbConfig config;

	azaMeters metersInput;
	azaMeters metersOutput;

	azaDelay inputDelay;
	azaDelay delays[AZAUDIO_REVERB_DELAY_COUNT];
	azaFilter filters[AZAUDIO_REVERB_DELAY_COUNT];
} azaReverb;

// initializes azaReverb in existing memory
void azaReverbInit(azaReverb *data, azaReverbConfig config);
// frees any additional memory that the azaReverb may have allocated
void azaReverbDeinit(azaReverb *data);
// Resets state. May be called automatically.
void azaReverbReset(azaReverb *data);
// Resets state for the specified channel range
void azaReverbResetChannels(azaReverb *data, uint32_t firstChannel, uint32_t channelCount);

// Convenience function that allocates and inits an azaReverb for you
// May return NULL indicating an out-of-memory error
azaReverb* azaMakeReverb(azaReverbConfig config);
// Frees an azaReverb that was created with azaMakeReverb
void azaFreeReverb(void *dsp);

azaDSP* azaMakeDefaultReverb();

int azaReverbProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);

// Reverb was going to report the specs from delay and filters, which may still be desired, but we know they're zeroed, so probably just ignore it for now?
// azaDSPSpecs azaReverbGetSpecs(void *dsp, uint32_t samplerate);



static const azaDSP azaReverbHeader = {
	/* .size         = */ sizeof(azaReverb),
	/* .version      = */ 1,
	/* .owned, bypass, selected, prevChannelCountDst, prevChannelCountSrc */ false, false, false, 0, 0,
	/* ._reserved    = */ {0},
	/* .error        = */ 0,
	/* .name         = */ "Reverb",
	/* fp_getSpecs   = */ NULL,
	/* fp_process    = */ azaReverbProcess,
	/* fp_free       = */ azaFreeReverb,
	NULL, NULL,
};



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZAREVERB_H
