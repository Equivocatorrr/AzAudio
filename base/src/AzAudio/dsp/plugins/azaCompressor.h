/*
	File: azaCompressor.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_AZACOMPRESSOR_H
#define AZAUDIO_AZACOMPRESSOR_H

#include "../azaDSP.h"
#include "../azaMeters.h"

#include "azaRMS.h"

#ifdef __cplusplus
extern "C" {
#endif



typedef struct azaCompressorConfig {
	// Activation threshold in dB
	float threshold;
	// positive values allow 1/ratio of the overvolume through
	// negative values subtract overvolume*ratio
	float ratio;
	// attack time in ms
	float attack_ms;
	// decay time in ms
	float decay_ms;
	// input gain in dB
	float gainInput;
	// output gain in dB
	float gainOutput;
	// TODO: Add sidechain support
	// Any effects to apply to the activation signal
	azaDSP *activationEffects;
} azaCompressorConfig;

typedef struct azaCompressor {
	azaDSP header;
	azaCompressorConfig config;

	azaMeters metersInput;
	azaMeters metersOutput;

	float attenuation;
	float minGain, minGainShort;
	azaRMS rms;
} azaCompressor;

// initializes azaCompressor in existing memory
void azaCompressorInit(azaCompressor *data, azaCompressorConfig config);
// frees any additional memory that the azaCompressor may have allocated
void azaCompressorDeinit(azaCompressor *data);
// Resets state. May be called automatically.
void azaCompressorReset(azaCompressor *data);
// Resets state for the specified channel range
void azaCompressorResetChannels(azaCompressor *data, uint32_t firstChannel, uint32_t channelCount);

// Convenience function that allocates and inits an azaCompressor for you
// May return NULL indicating an out-of-memory error
azaCompressor* azaMakeCompressor(azaCompressorConfig config);
// Frees an azaCompressor that was created with azaMakeCompressor
void azaFreeCompressor(void *dsp);

azaDSP* azaMakeDefaultCompressor();

int azaCompressorProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);



static const azaDSP azaCompressorHeader = {
	/* .size         = */ sizeof(azaCompressor),
	/* .version      = */ 1,
	/* .owned, bypass, selected, prevChannelCountDst, prevChannelCountSrc */ false, false, false, 0, 0,
	/* ._reserved    = */ {0},
	/* .name         = */ "Compressor",
	/* fp_getSpecs   = */ NULL,
	/* fp_process    = */ azaCompressorProcess,
	/* fp_free       = */ azaFreeCompressor,
	NULL, NULL,
};



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZACOMPRESSOR_H
