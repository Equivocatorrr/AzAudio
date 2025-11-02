/*
	File: azaDelay.c
	Author: Philip Haynes
*/

#include "azaDelay.h"

#include "../../AzAudio.h"
#include "../../math.h"
#include "../../error.h"

void azaDelayInit(azaDelay *data, azaDelayConfig config) {
	data->header = azaDelayHeader;
	data->config = config;
	azaDSPChainInit(&data->inputEffects, 0);
}

void azaDelayDeinit(azaDelay *data) {
	azaDSPChainDeinit(&data->inputEffects);
	if (data->buffer) {
		aza_free(data->buffer);
		data->buffer = NULL;
	}
}

void azaDelayReset(azaDelay *data) {
	azaMetersReset(&data->metersInput);
	azaMetersReset(&data->metersOutput);
	memset(data->buffer, 0, sizeof(data->buffer[0]) * data->bufferCap);
	for (uint8_t c = 0; c < AZA_MAX_CHANNEL_POSITIONS; c++) {
		data->channelData[c].index = 0;
	}
}

void azaDelayResetChannels(azaDelay *data, uint32_t firstChannel, uint32_t channelCount) {
	azaMetersResetChannels(&data->metersInput, firstChannel, channelCount);
	azaMetersResetChannels(&data->metersOutput, firstChannel, channelCount);
	for (uint8_t c = firstChannel; c < firstChannel+channelCount; c++) {
		memset(data->channelData[c].buffer, 0, sizeof(data->channelData[c].buffer[0]) * data->channelData[c].delaySamples);
		data->channelData[c].index = 0;
	}
}

azaDelay* azaMakeDelay(azaDelayConfig config) {
	azaDelay *result = aza_calloc(1, sizeof(azaDelay));
	if (result) {
		azaDelayInit(result, config);
	}
	return result;
}

void azaFreeDelay(void *data) {
	azaDelayDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultDelay() {
	return (azaDSP*)azaMakeDelay((azaDelayConfig) {
		.gainWet = -6.0f,
		.gainDry = 0.0f,
		.delay_ms = 300.0f,
		.feedback = 0.5f,
		.pingpong = 0.0f,
	});
}

static int azaDelayHandleBufferResizes(azaDelay *data, uint32_t samplerate, uint8_t channelCount) {
	uint32_t delaySamplesMax = 0;
	uint32_t perChannelBufferCap = data->bufferCap / channelCount;
	bool realloc = false;
	for (uint8_t c = 0; c < channelCount; c++) {
		azaDelayChannelData *channelData = &data->channelData[c];
		uint32_t delaySamples = (uint32_t)aza_ms_to_samples(data->config.delay_ms + channelData->config.delay_ms, (float)samplerate);
		if (delaySamples > delaySamplesMax) delaySamplesMax = delaySamples;
		if (channelData->delaySamples >= delaySamples) {
			if (channelData->index > delaySamples) {
				channelData->index = 0;
			}
			channelData->delaySamples = delaySamples;
		} else if (perChannelBufferCap >= delaySamples) {
			channelData->delaySamples = delaySamples;
		} else {
			realloc = true;
		}
	}
	if (!realloc) return AZA_SUCCESS;
	// Have to realloc buffer
	uint32_t newPerChannelBufferCap = (uint32_t)aza_grow(data->bufferCap / channelCount, delaySamplesMax, 256);
	// TODO: Probably use realloc? idk, if portioning changes we need to copy explicitly anyway
	float *newBuffer = aza_calloc(sizeof(float), newPerChannelBufferCap * channelCount);
	if (!newBuffer) return AZA_ERROR_OUT_OF_MEMORY;
	for (uint8_t c = 0; c < channelCount; c++) {
		azaDelayChannelData *channelData = &data->channelData[c];
		float *newChannelBuffer = newBuffer + c * newPerChannelBufferCap;
		if (data->buffer && channelData->delaySamples) {
			memcpy(newChannelBuffer, channelData->buffer, sizeof(float) * channelData->delaySamples);
		}
		channelData->buffer = newChannelBuffer;
		// We also have to set delaySamples since we didn't do it above
		channelData->delaySamples = (uint32_t)aza_ms_to_samples(data->config.delay_ms + channelData->config.delay_ms, (float)samplerate);
	}
	if (data->buffer) {
		aza_free(data->buffer);
	}
	data->buffer = newBuffer;
	return AZA_SUCCESS;
}

int azaDelayProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	// Bypass handled by azaDSPProcess
	int err = AZA_SUCCESS;
	assert(dsp != NULL);
	azaDelay *data = (azaDelay*)dsp;

	if AZA_UNLIKELY(flags & AZA_DSP_PROCESS_FLAG_CUT) {
		azaDelayReset(data);
	}

	err = azaCheckBuffersForDSPProcess(dst, src, /* sameFrameCount: */ true, /* sameChannelCount: */ true);
	if AZA_UNLIKELY(err) return err;

	err = azaDelayHandleBufferResizes(data, dst->samplerate, dst->channelLayout.count);
	if AZA_UNLIKELY(err) return err;

	if (dst->channelLayout.count > data->header.prevChannelCountDst) {
		azaDelayResetChannels(data, data->header.prevChannelCountDst, dst->channelLayout.count - data->header.prevChannelCountDst);
	}
	data->header.prevChannelCountDst = dst->channelLayout.count;

	if (data->header.selected) {
		azaMetersUpdate(&data->metersInput, src, 1.0f);
	}

	azaBuffer sideBuffer = azaPushSideBufferZero(src->frames, 0, 0, src->channelLayout.count, src->samplerate);
	for (uint8_t c = 0; c < src->channelLayout.count; c++) {
		azaDelayChannelData *channelData = &data->channelData[c];
		uint32_t index = channelData->index;
		for (uint32_t i = 0; i < src->frames; i++) {
			uint8_t c2 = (c + 1) % src->channelLayout.count;
			float toAdd = src->pSamples[i * src->stride + c] + channelData->buffer[index] * data->config.feedback;
			sideBuffer.pSamples[i * sideBuffer.stride + c] += toAdd * (1.0f - data->config.pingpong);
			sideBuffer.pSamples[i * sideBuffer.stride + c2] += toAdd * data->config.pingpong;
			index = (index+1) % channelData->delaySamples;
		}
	}
	if (data->inputEffects.steps.count) {
		err = azaDSPChainProcess(&data->inputEffects, &sideBuffer, &sideBuffer, flags);
		if AZA_UNLIKELY(err) goto error;
	}
	float amountWet = data->config.muteWet ? 0.0f : aza_db_to_ampf(data->config.gainWet);
	float amountDry = data->config.muteDry ? 0.0f : aza_db_to_ampf(data->config.gainDry);
	for (uint8_t c = 0; c < dst->channelLayout.count; c++) {
		azaDelayChannelData *channelData = &data->channelData[c];
		uint32_t index = channelData->index;
		for (uint32_t i = 0; i < dst->frames; i++) {
			channelData->buffer[index] = sideBuffer.pSamples[i * sideBuffer.stride + c];
			index = (index+1) % channelData->delaySamples;
			dst->pSamples[i * dst->stride + c] = channelData->buffer[index] * amountWet + src->pSamples[i * src->stride + c] * amountDry;
		}
		channelData->index = index;
	}

	if (data->header.selected) {
		azaMetersUpdate(&data->metersOutput, dst, 1.0f);
	}
error:
	azaPopSideBuffer();
	return err;
}

azaDSPSpecs azaDelayGetSpecs(void *dsp, uint32_t samplerate) {
	azaDelay *data = (azaDelay*)dsp;
	float maxChannelDelay_ms = 0.0f;
	for (uint8_t c = 0; c < data->header.prevChannelCountDst; c++) {
		maxChannelDelay_ms = azaMaxf(maxChannelDelay_ms, data->channelData[c].config.delay_ms);
	}
	float totalDelay_ms = data->config.delay_ms + maxChannelDelay_ms;
	return (azaDSPSpecs) {
		.latencyFrames = 0,
		.leadingFrames = (uint32_t)aza_ms_to_samples(totalDelay_ms, (float)samplerate),
	};
}