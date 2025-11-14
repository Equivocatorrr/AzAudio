/*
	File: azaLowPassFIR.c
	Author: Philip Haynes
*/

#include "azaLowPassFIR.h"

#include "../../AzAudio.h"
#include "../../error.h"
#include "../azaKernel.h"

#include "../../gui/gui.h"

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
		.maxKernelSamples = 13*16+1,
	});
}

// BIG TODO: Use half-pass filters for very low frequencies to reduce the total workload.

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

	uint32_t maxKernelRadius = (uint32_t)ceilf((float)data->config.maxKernelSamples / 2.0f);
	// maxKernelRadius = AZA_CLAMP(maxKernelRadius, 1, AZA_KERNEL_DEFAULT_LANCZOS_COUNT);

	if (src->leadingFrames < maxKernelRadius) {
		AZA_LOG_ERR("Error(%s): src->leadingFrames (%u) < maxKernelRadius(%u)\n", AZA_FUNCTION_NAME, src->leadingFrames, maxKernelRadius);
		return AZA_ERROR_INVALID_FRAME_COUNT;
	}
	if (src->trailingFrames < maxKernelRadius) {
		AZA_LOG_ERR("Error(%s): src->trailingFrames (%u) < maxKernelRadius(%u)\n", AZA_FUNCTION_NAME, src->trailingFrames, maxKernelRadius);
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
	if (dst->pSamples == src->pSamples) {
		// We progressively write into dst as we sample, and our kernel will sample the frames we put into dst if we don't do this.
		sideBuffer = azaPushSideBufferCopy(src);
		src = &sideBuffer;
		numSideBuffers++;
	}

	if (data->header.selected) {
		azaMetersUpdate(&data->metersInput, src, 1.0f);
	}

	float minNyquist = azaMinf((float)dst->samplerate, (float)src->samplerate) * 0.5f;

	float dstLen_ms = azaBufferGetLen_ms(dst);
	float deltaT = dstLen_ms / data->config.frequencyFollowTime_ms;
	float startFrequency = azaFollowerLinearUpdateTarget(&data->frequency, data->config.frequency, deltaT);
	startFrequency = azaMinf(startFrequency, minNyquist);
	float endFrequency = azaFollowerLinearGetValue(&data->frequency);
	endFrequency = azaMinf(endFrequency, minNyquist);
	float startKernelRate = azaMaxf(startFrequency / (0.5f * (float)src->samplerate), 0.011f);
	float endKernelRate = azaMaxf(endFrequency / (0.5f * (float)src->samplerate), 0.011f);

	// TODO: Handle popping from swapping out kernels dynamically like this.
	uint32_t actualRadius = azaKernelGetRadiusForRate(startKernelRate, maxKernelRadius);
	actualRadius = AZA_CLAMP(actualRadius, 1, AZA_KERNEL_DEFAULT_LANCZOS_COUNT);
	azaKernel *kernel = azaKernelGetDefaultLanczos(actualRadius);
	float minKernelRate = azaMinf(ceilf((float)kernel->length / 2.0f) / ceilf((float)data->config.maxKernelSamples / 2.0f), 1.0f);
	startKernelRate = azaClampf(startKernelRate, minKernelRate, 1.0f);
	endKernelRate = azaClampf(endKernelRate, minKernelRate, 1.0f);

	float srcFrame = data->srcFrameOffset;
	for (uint32_t i = 0; i < dst->frames; i++) {
		float t = (float)i / (float)dst->frames;
		float kernelRate = azaLerpf(startKernelRate, endKernelRate, t);
		int32_t frame = (int32_t)truncf(srcFrame);
		float fraction = i - (float)frame;

		azaBufferSampleWithKernel(dst->pSamples + i * dst->stride, dst->channelLayout.count, kernel, src, /* wrap */ false, frame, fraction, kernelRate);

		srcFrame += srcFrameRate;
	}

	if (data->header.selected) {
		azaMetersUpdate(&data->metersOutput, dst, 1.0f);
	}

	azaPopSideBuffers(numSideBuffers);
	return err;
}

azaDSPSpecs azaLowPassFIRGetSpecs(void *dsp, uint32_t samplerate) {
	azaLowPassFIR *data = (azaLowPassFIR*)dsp;
	uint32_t maxKernelRadius = (uint32_t)ceilf((float)data->config.maxKernelSamples / 2.0f);
	// maxKernelRadius = AZA_CLAMP(maxKernelRadius, 1, AZA_KERNEL_DEFAULT_LANCZOS_COUNT);
#if 0
	// Report latency based on frequency (causes continuous latency changes)
	float maxFrequency = azaMaxf(data->frequency.start, data->frequency.end);
	float kernelRate = azaMaxf(maxFrequency / (0.5f * (float)samplerate), 0.011f);
	maxKernelRadius = (uint32_t)ceilf((float)maxKernelRadius / kernelRate);
#endif
	azaDSPSpecs result = {
		.latencyFrames = 0,
		.leadingFrames = maxKernelRadius,
		.trailingFrames = maxKernelRadius,
	};
	return result;
}



// GUI



static const int meterDBRange = 48;
static const int meterDBHeadroom = 12;

void azagDrawLowPassFIR(void *dsp, azagRect bounds) {
	azaLowPassFIR *data = dsp;
	int usedWidth;
	usedWidth = azagDrawMeters(&data->metersInput, bounds, meterDBRange, meterDBHeadroom);
	azagRectShrinkLeftMargin(&bounds, usedWidth);

	usedWidth = azagDrawSliderFloatLog(bounds, &data->config.frequency, 50.0f, 24000.0f, 0.1f, 4000.0f, "Cutoff Frequency", "%.1fHz");
	azagRectShrinkLeftMargin(&bounds, usedWidth);

	usedWidth = azagDrawSliderFloatLog(bounds, &data->config.frequencyFollowTime_ms, 1.0f, 5000.0f, 0.2f, 50.0f, "Cutoff Frequency Follower Time", "%.0fms");
	azagRectShrinkLeftMargin(&bounds, usedWidth);

	float maxKernelSamples = data->config.maxKernelSamples;
	usedWidth = azagDrawSliderFloatLog(bounds, &maxKernelSamples, 16.0f, 2.0f*4192.0f, 1.0f, 63, "Maximum Kernel Samples Per Sample (Quality)", "%.0f");
	data->config.maxKernelSamples = (uint16_t)roundf(maxKernelSamples);
	azagRectShrinkLeftMargin(&bounds, usedWidth);

	usedWidth = azagDrawMeters(&data->metersOutput, bounds, meterDBRange, meterDBHeadroom);
	azagRectShrinkLeftMargin(&bounds, usedWidth);
}