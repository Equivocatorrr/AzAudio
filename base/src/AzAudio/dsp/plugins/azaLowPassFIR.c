/*
	File: azaLowPassFIR.c
	Author: Philip Haynes
*/

#include "azaLowPassFIR.h"

#include "../../AzAudio.h"
#include "../../error.h"
#include "../azaKernel.h"

void azaLowPassFIRInit(azaLowPassFIR *data, azaLowPassFIRConfig config) {
	data->header = azaLowPassFIRHeader;
	data->config = config;
	data->srcFrameOffset = 0.0f;
	azaFollowerLinearJump(&data->frequency, config.frequency);
}

void azaLowPassFIRDeinit(azaLowPassFIR *data) {
	// We good :)
}

void azaLowPassFIRReset(azaLowPassFIR *data) {
	azaMetersReset(&data->metersInput);
	azaMetersReset(&data->metersOutput);
	data->srcFrameOffset = 0.0f;
}

void azaLowPassFIRResetChannels(azaLowPassFIR *data, uint32_t firstChannel, uint32_t channelCount) {
	azaMetersResetChannels(&data->metersInput, firstChannel, channelCount);
	azaMetersResetChannels(&data->metersOutput, firstChannel, channelCount);
}

azaLowPassFIR* azaMakeLowPassFIR(azaLowPassFIRConfig config) {
	azaLowPassFIR *result = aza_calloc(1, sizeof(azaLowPassFIR));
	if (result) {
		 azaLowPassFIRInit(result, config);
	}
	return result;
}

void azaFreeLowPassFIR(void *dsp) {
	azaLowPassFIRDeinit(dsp);
	aza_free(dsp);
}

azaDSP* azaMakeDefaultLowPassFIR() {
	return (azaDSP*)azaMakeLowPassFIR((azaLowPassFIRConfig) {
		.frequency = 4000.0f,
		.frequencyFollowTime_ms = 50.0f,
		.maxKernelSamples = 27,
	});
}

int azaLowPassFIRProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	// Bypass handled by azaDSPProcess
	int err = AZA_SUCCESS;
	uint8_t numSideBuffers = 0;
	assert(dsp != NULL);
	azaLowPassFIR *data = (azaLowPassFIR*)dsp;

	if AZA_UNLIKELY(flags & AZA_DSP_PROCESS_FLAG_CUT) {
		azaLowPassFIRReset(data);
	}

	err = azaCheckBuffersForDSPProcess(dst, src, /* sameFrameCount: */ false, /* sameChannelCount: */ true);
	if AZA_UNLIKELY(err) return err;

	uint32_t maxKernelRadius = (data->config.maxKernelSamples-1)/2;
	maxKernelRadius = AZA_CLAMP(maxKernelRadius, 1, AZA_KERNEL_DEFAULT_LANCZOS_COUNT);

	if (src->framesLeading < maxKernelRadius) {
		AZA_LOG_ERR("Error(%s): src->framesLeading (%u) < maxKernelRadius(%u)\n", AZA_FUNCTION_NAME, src->framesLeading, maxKernelRadius);
		return AZA_ERROR_INVALID_FRAME_COUNT;
	}
	if (src->framesTrailing < maxKernelRadius) {
		AZA_LOG_ERR("Error(%s): src->framesTrailing (%u) < maxKernelRadius(%u)\n", AZA_FUNCTION_NAME, src->framesTrailing, maxKernelRadius);
		return AZA_ERROR_INVALID_FRAME_COUNT;
	}
	// frame delta in src for one frame into dst
	float srcFrameRate = (float)src->samplerate / (float)dst->samplerate;
	uint32_t srcFramesNeeded = (uint32_t)ceilf((float)dst->frames * srcFrameRate);
	if (src->frames < srcFramesNeeded) {
		AZA_LOG_ERR("Error(%s): src->frames (%u) < srcFramesNeeded(%u) where srcFramesNeeded = ceil(dst->frames(%u) * src->samplerate(%u) / dst->samplerate(%u))\n", AZA_FUNCTION_NAME, src->frames, srcFramesNeeded, dst->frames, src->samplerate, dst->samplerate);
		return AZA_ERROR_INVALID_FRAME_COUNT;
	}
	azaBuffer sideBuffer;
	if (dst == src) {
		// We progressively write into dst as we sample, and our kernel will sample the frames we put into dst if we don't do this.
		sideBuffer = azaPushSideBufferCopy(src);
		src = &sideBuffer;
		numSideBuffers++;
	}

	float minNyquist = azaMinf((float)dst->samplerate, (float)src->samplerate) * 0.5f;

	float dstLen_ms = azaBufferGetLen_ms(dst);
	float deltaT = dstLen_ms / data->config.frequencyFollowTime_ms;
	float startFrequency = azaFollowerLinearUpdateTarget(&data->frequency, data->config.frequency, deltaT);
	startFrequency = azaMinf(startFrequency, minNyquist);
	float endFrequency = azaFollowerLinearGetValue(&data->frequency);
	endFrequency = azaMinf(endFrequency, minNyquist);
	float startKernelRate = 0.5f * (float)src->samplerate / startFrequency;
	float endKernelRate = 0.5f * (float)src->samplerate / endFrequency;

	// TODO: Handle popping from swapping out kernels dynamically like this.
	azaKernel *kernel = azaKernelGetDefaultLanczos(azaKernelGetRadiusForRate(startKernelRate, maxKernelRadius));

	float srcFrame = data->srcFrameOffset;
	for (uint32_t i = 0; i < dst->frames; i++) {
		float t = (float)i / (float)dst->frames;
		float kernelRate = azaLerpf(startKernelRate, endKernelRate, t);
		int32_t frame = (int32_t)truncf(srcFrame);
		float fraction = i - (float)frame;

		azaBufferSampleWithKernel(dst->pSamples + i * dst->stride, dst->channelLayout.count, kernel, src, /* wrap */ false, frame, fraction, kernelRate);

		srcFrame += srcFrameRate;
	}
	azaPopSideBuffers(numSideBuffers);
	return err;
}

azaDSPSpecs azaLowPassFIRGetSpecs(void *dsp, uint32_t samplerate) {
	azaLowPassFIR *data = (azaLowPassFIR*)dsp;
	uint32_t maxKernelRadius = (data->config.maxKernelSamples-1)/2;
	maxKernelRadius = AZA_CLAMP(maxKernelRadius, 1, AZA_KERNEL_DEFAULT_LANCZOS_COUNT);
	azaDSPSpecs result = {
		.latencyFrames = maxKernelRadius,
		.leadingFrames = maxKernelRadius,
		.trailingFrames = maxKernelRadius,
	};
	return result;
}
