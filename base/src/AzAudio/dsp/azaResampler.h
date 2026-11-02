/*
	File: azaResampler.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_AZARESAMPLER_H
#define AZAUDIO_AZARESAMPLER_H

#include "azaKernel.h"

#ifdef __cplusplus
extern "C" {
#endif


// Helper struct for resampling a stream
typedef struct azaResampler {
	azaKernel *kernel;
	float inputFrameStart;

	// Persistent buffer for carrying over samples across iterations
	azaBuffer input;
} azaResampler;

void azaResamplerInit(azaResampler *data, azaKernel *kernel);
void azaResamplerDeinit(azaResampler *data);

// Returns the latency in terms of input frames
uint32_t azaResamplerGetLatency(azaResampler *data);

// How many frames can be served by the current input including any held over since the last time.
// Used for if the input size is given but the output size can change so we don't overflow or underflow the input buffer.
uint32_t azaResamplerGetNextOutputCount(azaResampler *data, uint32_t outputSamplerate, azaBuffer *input);
// How many frames should we ingest in the input to meet the output space requirements, factoring in any held over frames since last time.
// Used for if the output size is given but the input size can change so we don't overflow or underflow the input buffer.
uint32_t azaResamplerGetNextInputCount(azaResampler *data, azaBuffer *output, uint32_t inputSamplerate);

// Ingest the input buffer and put the result into the output buffer
int azaResamplerResample(azaResampler *data, azaBuffer *output, azaBuffer *input);


#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZARESAMPLER_H