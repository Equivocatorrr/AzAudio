/*
	File: azaResampler.c
	Author: Philip Haynes
*/

#include "azaResampler.h"
#include "../error.h"

void azaResamplerInit(azaResampler *data, azaKernel *kernel) {
	data->kernel = kernel;
	data->inputFrameStart = 0.0f;
	memset(&data->input, 0, sizeof(data->input));
}

void azaResamplerDeinit(azaResampler *data) {
	azaBufferDeinit(&data->input, false);
}

uint32_t azaResamplerGetLatency(azaResampler *data) {
	if (!data || !data->kernel) return AZA_ERROR_NULL_POINTER;
	return data->kernel->length-1;
}

uint32_t azaResamplerGetOutputFrameCount(azaResampler *data, uint32_t outputSamplerate, uint32_t inputFrames, uint32_t inputSamplerate) {
	return azaGetResampledDstFrameCount(outputSamplerate, inputSamplerate, inputFrames);
}

uint32_t azaResamplerGetNextOutputCount(azaResampler *data, uint32_t outputSamplerate, azaBuffer *input) {
	assert(outputSamplerate);
	assert(input->samplerate);
	float factor = (float)outputSamplerate / (float)input->samplerate;
	float outputFrameEnd = ((float)data->input.frames - data->inputFrameStart + (float)input->frames) * factor;
	return (uint32_t)floorf(outputFrameEnd);
}

uint32_t azaResamplerGetNextInputCount(azaResampler *data, azaBuffer *output, uint32_t inputSamplerate) {
	assert(output->samplerate);
	assert(inputSamplerate);
	float factor = (float)inputSamplerate / (float)output->samplerate;
	float inputFrameEnd = data->inputFrameStart - (float)data->input.frames + (float)output->frames * factor;
	return (uint32_t)ceilf(inputFrameEnd);
}

static int azaResamplerPrimeBuffer(azaResampler *data, azaBuffer *input) {
	assert(data);
	assert(input);
	uint32_t framesCompleted = (uint32_t)floorf(data->inputFrameStart);
	data->inputFrameStart -= (float)framesCompleted;
	assert(framesCompleted <= data->input.frames);
	uint32_t framesHoldover = data->input.frames - framesCompleted;
	int err;
	err = azaBufferResize(
		&data->input,
		input->frames+framesHoldover,
		data->kernel->sampleZero,
		data->kernel->length-1-data->kernel->sampleZero,
		input->channelLayout
	);
	if (err) return err;
	data->input.samplerate = input->samplerate;
	uint32_t carryFrames = data->kernel->length-1 + framesHoldover;
	uint32_t channels = data->input.channelLayout.count;
	float *start = data->input.pSamples-data->input.leadingFrames*channels;
	memmove(start, start + framesCompleted*channels, sizeof(float)*carryFrames*channels);
	memcpy(start + carryFrames*channels, input->pSamples, sizeof(float)*input->frames*channels);
	return AZA_SUCCESS;
}

int azaResamplerResample(azaResampler *data, azaBuffer *output, azaBuffer *input) {
	if (!data) return AZA_ERROR_NULL_POINTER;
	int err;
	err = azaCheckBuffer(output);
	if (err) return err;
	err = azaCheckBuffer(input);
	if (err) return err;
	assert(output->samplerate);
	assert(input->samplerate);
	err = azaResamplerPrimeBuffer(data, input);
	if (err) return err;

	azaBufferResample(output, &data->input, data->kernel, &data->inputFrameStart);

	return AZA_SUCCESS;
}