/*
	File: azaGate.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_AZAGATE_H
#define AZAUDIO_AZAGATE_H

#include "../azaDSP.h"
#include "../azaMeters.h"

#include "azaRMS.h"

#ifdef __cplusplus
extern "C" {
#endif



typedef struct azaGateConfig {
	// cutoff threshold in dB
	float threshold;
	// For signals below the threshold, ratio multiplies the negative gain delta
	// i.e. With a threshold of -6dB and a signal of -12dB, a ratio of 3 would output a -24dB signal
	//     input - threshold   =  delta = -12 - -6     =  -6
	// threshold + delta*ratio = output =  -6 + -6 * 3 = -24
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
} azaGateConfig;

typedef struct azaGate {
	azaDSP header;
	azaGateConfig config;

	// Any effects to apply to the activation signal
	azaDSPChain activationEffects;

	azaMeters metersInput;
	azaMeters metersOutput;

	float attenuation;
	float gain;
	azaRMS rms;
} azaGate;

// initializes azaGate in existing memory
void azaGateInit(azaGate *data, azaGateConfig config);
// frees any additional memory that the azaGate may have allocated
void azaGateDeinit(azaGate *data);
// Resets state. May be called automatically.
void azaGateReset(azaGate *data);
// Resets state for the specified channel range
void azaGateResetChannels(azaGate *data, uint32_t firstChannel, uint32_t channelCount);

// Convenience function that allocates and inits an azaGate for you
// May return NULL indicating an out-of-memory error
azaGate* azaMakeGate(azaGateConfig config);
// Frees an azaGate that was created with azaMakeGate
void azaFreeGate(void *dsp);

azaDSP* azaMakeDefaultGate();

int azaGateProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);



static const azaDSP azaGateHeader = {
	/* .size         = */ sizeof(azaGate),
	/* .version      = */ 1,
	/* .owned, bypass, selected, prevChannelCountDst, prevChannelCountSrc */ false, false, false, 0, 0,
	/* ._reserved    = */ {0},
	/* .error        = */ 0,
	/* .name         = */ "Gate",
	/* fp_getSpecs   = */ NULL,
	/* fp_process    = */ azaGateProcess,
	/* fp_free       = */ azaFreeGate,
};



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZAGATE_H
