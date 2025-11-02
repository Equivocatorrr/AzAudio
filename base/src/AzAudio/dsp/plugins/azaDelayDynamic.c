/*
	File: azaDelayDynamic.c
	Author: Philip Haynes
*/

#include "azaDelayDynamic.h"

#include "../../AzAudio.h"
#include "../../math.h"
#include "../../error.h"

enum { AZA_DELAY_DYNAMIC_DESIRED_KERNEL_RADIUS = 13 };

#define AZA_DELAY_DYNAMIC_FIXED_RADIUS 0

static azaKernel* azaDelayDynamicGetKernel(azaDelayDynamic *data, float rate) {
	azaKernel *kernel = data->config.kernel;
	if (!kernel) {
#if AZA_DELAY_DYNAMIC_FIXED_RADIUS
		kernel = azaKernelGetDefaultLanczos(AZA_DELAY_DYNAMIC_DESIRED_KERNEL_RADIUS);
#else
		kernel = azaKernelGetDefaultLanczos(azaKernelGetRadiusForRate(rate, AZA_DELAY_DYNAMIC_DESIRED_KERNEL_RADIUS));
#endif
	}
	return kernel;
}

static int azaDelayDynamicHandleBufferResizes(azaDelayDynamic *data, azaBuffer *src) {
	// TODO: Probably track channel layouts and handle them changing. Right now the buffers will break if the number of channels changes.
	azaKernel *kernel = azaDelayDynamicGetKernel(data, 1.0f);
	uint32_t kernelSamples = kernel->length;
	// uint32_t kernelSamples = (uint32_t)ceilf((float)kernel->length / delayDynamicDefaultRate);
	uint32_t delaySamplesMax = (uint32_t)ceilf(aza_ms_to_samples(data->config.delayMax_ms, (float)src->samplerate)) + kernelSamples;
	uint32_t totalSamplesNeeded = delaySamplesMax + src->frames;
	uint32_t perChannelBufferCap = data->bufferCap / src->channelLayout.count;
	if (perChannelBufferCap >= totalSamplesNeeded) return AZA_SUCCESS;
	// Have to realloc buffer
	uint32_t newPerChannelBufferCap = (uint32_t)aza_grow(perChannelBufferCap, totalSamplesNeeded, 256);
	float *newBuffer = aza_calloc(sizeof(float), newPerChannelBufferCap * src->channelLayout.count);
	if (!newBuffer) return AZA_ERROR_OUT_OF_MEMORY;
	for (uint8_t c = 0; c < src->channelLayout.count; c++) {
		azaDelayDynamicChannelData *channelData = &data->channelData[c];
		float *newChannelBuffer = newBuffer + c * newPerChannelBufferCap;
		if (data->buffer) {
			memcpy(newChannelBuffer + newPerChannelBufferCap - perChannelBufferCap, channelData->buffer, sizeof(float) * perChannelBufferCap);
		}
		channelData->buffer = newChannelBuffer;
	}
	for (uint8_t c = src->channelLayout.count; c < AZA_MAX_CHANNEL_POSITIONS; c++) {
		// If channel count shrinks, prevent the above from breaking if it grows again
		data->channelData[c].buffer = NULL;
		data->channelData[c].ratePrevious = 0.0f;
	}
	if (data->buffer) {
		aza_free(data->buffer);
	}
	data->buffer = newBuffer;
	data->bufferCap = newPerChannelBufferCap * src->channelLayout.count;
	return AZA_SUCCESS;
}

// Puts new audio data into the buffer for immediate sampling. Assumes azaDelayDynamicHandleBufferResizes was called already.
static void azaDelayDynamicPrimeBuffer(azaDelayDynamic *data, azaBuffer *src) {
	azaKernel *kernel = azaDelayDynamicGetKernel(data, 1.0f);
	uint32_t kernelSamples = kernel->length;
	// uint32_t kernelSamples = (uint32_t)ceilf((float)kernel->length / delayDynamicDefaultRate);
	uint32_t delaySamplesMax = (uint32_t)ceilf(aza_ms_to_samples(data->config.delayMax_ms, (float)src->samplerate)) + kernelSamples;
	for (uint8_t c = 0; c < src->channelLayout.count; c++) {
		azaDelayDynamicChannelData *channelData = &data->channelData[c];
		// Move existing buffer back to make room for new buffer data
		if AZA_LIKELY(data->lastSrcBufferFrames) {
			memmove(channelData->buffer, channelData->buffer + data->lastSrcBufferFrames, sizeof(*channelData->buffer) * delaySamplesMax);
		}
		azaBufferCopyChannel(&(azaBuffer) {
			.pSamples = channelData->buffer + delaySamplesMax,
			.samplerate = src->samplerate,
			.frames = src->frames,
			.stride = 1,
			.channelLayout = (azaChannelLayout) { .count = 1 },
		}, 0, src, c);
	}
	data->lastSrcBufferFrames = src->frames;
}



void azaDelayDynamicInit(azaDelayDynamic *data, azaDelayDynamicConfig config) {
	data->header = azaDelayDynamicHeader;
	data->config = config;
	azaDSPChainInit(&data->inputEffects, 0);
	data->lastSrcBufferFrames = 0;
}

void azaDelayDynamicDeinit(azaDelayDynamic *data) {
	azaDSPChainDeinit(&data->inputEffects);
	if (data->buffer) {
		aza_free(data->buffer);
	}
	data->buffer = NULL;
	data->bufferCap = 0;
}

void azaDelayDynamicReset(azaDelayDynamic *data) {
	azaMetersReset(&data->metersInput);
	azaMetersReset(&data->metersOutput);
	// This might be called before we allocate anything, so be smart about it
	if (data->buffer) {
		memset(data->buffer, 0, sizeof(data->buffer[0]) * data->bufferCap);
	}
}

void azaDelayDynamicResetChannels(azaDelayDynamic *data, uint32_t firstChannel, uint32_t channelCount) {
	azaMetersResetChannels(&data->metersInput, firstChannel, channelCount);
	azaMetersResetChannels(&data->metersOutput, firstChannel, channelCount);
	// This might be called before we allocate anything, so be smart about it
	if (data->channelData[firstChannel].buffer) {
		size_t bufferOffset = data->channelData[firstChannel].buffer - data->buffer;
		memset(data->channelData[firstChannel].buffer, 0, sizeof(data->buffer[0]) * (data->bufferCap - bufferOffset));
	}
}

azaDelayDynamic* azaMakeDelayDynamic(azaDelayDynamicConfig config) {
	azaDelayDynamic *result = aza_calloc(1, sizeof(azaDelayDynamic));
	if (result) {
		 azaDelayDynamicInit(result, config);
	}
	return result;
}

void azaFreeDelayDynamic(void *data) {
	azaDelayDynamicDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultDelayDynamic() {
	return (azaDSP*)azaMakeDelayDynamic((azaDelayDynamicConfig) {
		.gainWet = -6.0f,
		.gainDry = 0.0f,
		.delayMax_ms = 500.0f,
		.delayFollowTime_ms = 20.0f, // Just under 60fps as a random guess, using buffer time as an unreliable half-measure to prevent pitch from changing abruptly because the next target hadn't come in yet. This needs a proper solution before long.
		.feedback = 0.5f,
		.pingpong = 0.0f,
		.kernel = NULL,
	});
}

int azaDelayDynamicProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	// Bypass handled by azaDSPProcess
	int err = AZA_SUCCESS;
	assert(dsp != NULL);
	azaDelayDynamic *data = (azaDelayDynamic*)dsp;

	if AZA_UNLIKELY(flags & AZA_DSP_PROCESS_FLAG_CUT) {
		azaDelayDynamicReset(data);
	}

	err = azaCheckBuffersForDSPProcess(dst, src, /* sameFrameCount: */ true, /* sameChannelCount: */ true);
	if AZA_UNLIKELY(err) return err;

	err = azaDelayDynamicHandleBufferResizes(data, src);
	if AZA_UNLIKELY(err) return err;

	if (dst->channelLayout.count > data->header.prevChannelCountDst) {
		azaDelayDynamicResetChannels(data, data->header.prevChannelCountDst, dst->channelLayout.count - data->header.prevChannelCountDst);
	}
	data->header.prevChannelCountDst = dst->channelLayout.count;

	if (data->header.selected) {
		azaMetersUpdate(&data->metersInput, src, 1.0f);
	}

	azaKernel *kernel = azaDelayDynamicGetKernel(data, 1.0f);
	azaBuffer sideBuffer = azaPushSideBufferCopy(src);
	int kernelSamplesLeft = kernel->sampleZero;
	int kernelSamplesRight = kernel->length - kernel->sampleZero;
	uint32_t delaySamplesMax = (uint32_t)ceilf(aza_ms_to_samples(data->config.delayMax_ms, (float)src->samplerate));

	if (data->config.feedback != 0.0f) {
		// Prime the input buffer with our feedback
		for (uint8_t c = 0; c < sideBuffer.channelLayout.count; c++) {
			azaDelayDynamicChannelData *channelData = &data->channelData[c];
			azaDelayDynamicChannelConfig *channelConfig = &data->config.channels[c];
			// Backup because we loop again below over the same range
			azaFollowerLinear followerBackup = channelData->delay_ms;
			azaFollowerLinearSetTarget(&channelData->delay_ms, channelConfig->delay_ms);

			float deltaT = (float)dst->frames / aza_ms_to_samples(data->config.delayFollowTime_ms, (float)sideBuffer.samplerate);
			float delayStart_ms = azaClampf(azaFollowerLinearUpdate(&channelData->delay_ms, deltaT), 0.0f, data->config.delayMax_ms);
			float delayEnd_ms = azaClampf(azaFollowerLinearGetValue(&channelData->delay_ms), 0.0f, data->config.delayMax_ms);
			float startIndex = (float)delaySamplesMax - aza_ms_to_samples(delayStart_ms, (float)dst->samplerate);
			float endIndex = (float)delaySamplesMax - aza_ms_to_samples(delayEnd_ms, (float)dst->samplerate) + (float)dst->frames;
			float endRate = azaMinf((endIndex - startIndex) / (float)dst->frames, 1.0f);

			// Very low rates will make the kernel sampling take much longer (1 / rate times as long as normal for a static kernel)
			if (endRate <= 0.01f) {
				azaBuffer oneChannel = azaBufferOneChannel(dst, c);
				azaBufferZero(&oneChannel);
				continue;
			}
			float startRate = channelData->ratePrevious != 0.0f ? channelData->ratePrevious : endRate;
			channelData->ratePrevious = endRate;
			kernel = azaDelayDynamicGetKernel(data, startRate);
			uint8_t c2 = (c + 1) % sideBuffer.channelLayout.count;
			for (uint32_t i = 0; i < sideBuffer.frames; i++) {
				float t = (float)i / (float)dst->frames;
				float rate = azaLerpf(startRate, endRate, t);
				float index = azaLerpf(startIndex, endIndex, t);
				int32_t frame = (int32_t)truncf(index);
				float fraction = index - (float)frame;
				float toAdd = azaSampleWithKernel1Ch(kernel, channelData->buffer+kernelSamplesLeft, 1, -kernelSamplesLeft, delaySamplesMax+kernelSamplesRight+sideBuffer.frames, false, frame, fraction, rate) * data->config.feedback;
				sideBuffer.pSamples[i * sideBuffer.stride + c] += toAdd * (1.0f - data->config.pingpong);
				sideBuffer.pSamples[i * sideBuffer.stride + c2] += toAdd * data->config.pingpong;
			}
			channelData->delay_ms = followerBackup;
		}
	}
	if (data->inputEffects.steps.count) {
		err = azaDSPChainProcess(&data->inputEffects, &sideBuffer, &sideBuffer, flags);
		if AZA_UNLIKELY(err) {
			goto error;
		}
	}
	azaDelayDynamicPrimeBuffer(data, &sideBuffer);
	float amountWet = data->config.muteWet ? 0.0f : aza_db_to_ampf(data->config.gainWet);
	float amountDry = data->config.muteDry ? 0.0f : aza_db_to_ampf(data->config.gainDry);
	for (uint8_t c = 0; c < dst->channelLayout.count; c++) {
		azaDelayDynamicChannelData *channelData = &data->channelData[c];
		azaDelayDynamicChannelConfig *channelConfig = &data->config.channels[c];
		azaFollowerLinearSetTarget(&channelData->delay_ms, channelConfig->delay_ms);

		float deltaT = (float)dst->frames / aza_ms_to_samples(data->config.delayFollowTime_ms, (float)sideBuffer.samplerate);
		float delayStart_ms = azaClampf(azaFollowerLinearUpdate(&channelData->delay_ms, deltaT), 0.0f, data->config.delayMax_ms);
		float delayEnd_ms = azaClampf(azaFollowerLinearGetValue(&channelData->delay_ms), 0.0f, data->config.delayMax_ms);
		float startIndex = (float)delaySamplesMax - aza_ms_to_samples(delayStart_ms, (float)dst->samplerate);
		float endIndex = (float)delaySamplesMax - aza_ms_to_samples(delayEnd_ms, (float)dst->samplerate) + (float)dst->frames;
		float endRate = azaMinf((endIndex - startIndex) / (float)dst->frames, 1.0f);

		// Very low rates will make the kernel sampling take much longer (1 / rate times as long as normal for a static kernel)
		if (endRate <= 0.01f) {
			azaBuffer oneChannel = azaBufferOneChannel(dst, c);
			azaBufferZero(&oneChannel);
			continue;
		}
		float startRate = channelData->ratePrevious != 0.0f ? channelData->ratePrevious : endRate;
		channelData->ratePrevious = endRate;
		// TODO: Swapping kernels by radius gets us nice, predictable performance costs, but without any interpolation between them, the jump in kernel radius creates a very quiet pop in the sampled audio. Using interpolation like that doubles our kernel sampling costs, which is already the most expensive part of this whole process.
		kernel = azaDelayDynamicGetKernel(data, startRate);
		for (uint32_t i = 0; i < dst->frames; i++) {
			float t = (float)i / (float)dst->frames;
			float rate = azaLerpf(startRate, endRate, t);
			float index = azaLerpf(startIndex, endIndex, t);
			int32_t frame = (int32_t)truncf(index);
			float fraction = index - (float)frame;
			float wet = azaSampleWithKernel1Ch(kernel, channelData->buffer+kernelSamplesLeft, 1, -kernelSamplesLeft, delaySamplesMax+kernelSamplesRight+src->frames, false, frame, fraction, rate);
			dst->pSamples[i * dst->stride + c] = wet * amountWet + src->pSamples[i * src->stride + c] * amountDry;
		}
	}

	if (data->header.selected) {
		azaMetersUpdate(&data->metersOutput, dst, 1.0f);
	}

error:
	azaPopSideBuffer();
	return err;
}

azaDSPSpecs azaDelayDynamicGetSpecs(void *dsp, uint32_t samplerate) {
	azaDelayDynamic *data = dsp;
	azaDSPSpecs specs = {0};
	azaKernel *kernel = azaDelayDynamicGetKernel(data, 1.0f);
	specs.latencyFrames = 0;
	specs.leadingFrames = kernel->sampleZero-1;
	specs.trailingFrames = kernel->length - kernel->sampleZero;
	return specs;
}



// Utilities



void azaDelayDynamicSetRamps(azaDelayDynamic *data, uint8_t numChannels, float startDelay_ms[], float endDelay_ms[], uint32_t frames, uint32_t samplerate) {
	data->config.delayFollowTime_ms = aza_samples_to_ms((float)frames, (float)samplerate);
	for (uint8_t c = 0; c < numChannels; c++) {
		azaFollowerLinearJump(&data->channelData[c].delay_ms, startDelay_ms[c]);
		data->config.channels[c].delay_ms = endDelay_ms[c];
	}
}
