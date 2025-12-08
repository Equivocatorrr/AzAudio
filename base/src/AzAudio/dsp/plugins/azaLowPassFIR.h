/*
	File: azaLowPassFIR.h
	Author: Philip Haynes
	A brick-wall lowpass filter, for resampling things or otherwise.
	Allows src and dst to be different samplerates.
	Currently just uses a lanczos kernel. We'll probably extend it in the future to allow a minimum-phase kernel for reduced latency.
*/

#ifndef AZAUDIO_AZALOWPASSFIR_H
#define AZAUDIO_AZALOWPASSFIR_H

#include "../azaDSP.h"
#include "../azaKernel.h"
#include "../azaMeters.h"
#include "../utility.h" // azaFollowerLinear

#ifdef __cplusplus
extern "C" {
#endif



extern const azaDSP azaLowPassFIRHeader;

typedef struct azaLowPassFIRConfig {
	float frequency;
	float frequencyFollowTime_ms;
	// if nonzero, sets the upper bound of how many samples to take from an azaKernel (which will increase as frequency decreases)
	// We use this information to decide the size of the kernel on the fly.
	// If zero, we just pick an okay default.
	uint16_t maxKernelSamples;
	aza_byte _reserved[2];
} azaLowPassFIRConfig;

typedef struct azaLowPassFIR {
	azaDSP dsp;
	azaLowPassFIRConfig config;

	azaMeters metersInput;
	azaMeters metersOutput;
	// Because it's not required that src and dst have the same samplerates (and therefore frame count) we need to keep track of our offset into src between processing, else we'd have a pop.
	float srcFrameOffset;

	azaFollowerLinear frequency;
} azaLowPassFIR;

// initializes azaLowPassFIR in existing memory
void azaLowPassFIRInit(azaLowPassFIR *data, azaLowPassFIRConfig config);
// frees any additional memory that the azaLowPassFIR may have allocated
void azaLowPassFIRDeinit(azaLowPassFIR *data);
// Resets state. May be called automatically.
void azaLowPassFIRReset(azaLowPassFIR *data);
// Resets state for the specified channel range
void azaLowPassFIRResetChannels(azaLowPassFIR *data, uint32_t firstChannel, uint32_t channelCount);

// Convenience function that allocates and inits an azaLowPassFIR for you
// May return NULL indicating an out-of-memory error
azaLowPassFIR* azaLowPassFIRMake(azaLowPassFIRConfig config);
// Frees an azaLowPassFIR that was created with azaLowPassFIRMake
void azaLowPassFIRFree(azaDSP *dsp);

azaDSP* azaLowPassFIRMakeDefault();
azaDSP* azaLowPassFIRMakeDuplicate(azaDSP *src);
int azaLowPassFIRCopyConfig(azaDSP *dst, azaDSP *src);

int azaLowPassFIRProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);

// LowPassFIR's sampling kernel causes there to be a minimum latency requirement, so we'll report that here
azaDSPSpecs azaLowPassFIRGetSpecs(azaDSP *dsp, uint32_t samplerate);



void azaLowPassFIRDraw(azaDSP *dsp, azagRect bounds);



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZALOWPASSFIR_H
