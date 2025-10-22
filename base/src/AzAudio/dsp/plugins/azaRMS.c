/*
	File: azaRMS.c
	Author: Philip Haynes
*/

#include "azaRMS.h"

#include "../../AzAudio.h"
#include "../../math.h"
#include "../../error.h"



void azaRMSInit(azaRMS *data, azaRMSConfig config) {
	data->header = azaRMSHeader;
	data->config = config;
	data->index = 0;
	data->bufferCap = 0;
	data->buffer = NULL;
}

void azaRMSDeinit(azaRMS *data) {
	if (data->bufferCap > 0) {
		aza_free(data->buffer);
	}
}

void azaRMSReset(azaRMS *data) {
	data->index = 0;
	if (data->buffer) {
		memset(data->buffer, 0, sizeof(data->buffer[0]) * data->bufferCap);
	}
	memset(data->channelData, 0, sizeof(data->channelData));
}

void azaRMSResetChannels(azaRMS *data, uint32_t firstChannel, uint32_t channelCount) {
	memset(data->channelData + firstChannel, 0, sizeof(data->channelData[0]) * channelCount);
	if (data->buffer) {
		float *start = data->buffer + data->config.windowSamples * firstChannel;
		float *end = start + AZA_MIN(data->config.windowSamples * channelCount, data->bufferCap);
		size_t count = end - start;
		memset(start, 0, sizeof(data->buffer[0]) * count);
	}
}

azaRMS* azaMakeRMS(azaRMSConfig config) {
	// return NULL; // Fake error to test Mixer GUI error reporting
	azaRMS *result = aza_calloc(1, sizeof(azaRMS));
	if (result) {
		azaRMSInit(result, config);
	}
	return result;
}

void azaFreeRMS(void *data) {
	azaRMSDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultRMS() {
	return (azaDSP*)azaMakeRMS((azaRMSConfig) {
		.windowSamples = 512,
		.combineOp = NULL,
	});
}

static int azaHandleRMSBuffer(azaRMS *data, uint8_t channels) {
	if (data->bufferCap < data->config.windowSamples * channels) {
		uint32_t newBufferCap = (uint32_t)aza_grow(data->bufferCap, data->config.windowSamples * channels, 32);
		data->buffer = aza_realloc(data->buffer, newBufferCap * sizeof(float));
		if (!data->buffer) {
			return AZA_ERROR_OUT_OF_MEMORY;
		}
		data->bufferCap = newBufferCap;
		azaRMSReset(data);
	}
	return AZA_SUCCESS;
}

int azaRMSProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	// Bypass handled by azaDSPProcess
	int err = AZA_SUCCESS;
	assert(dsp != NULL);
	azaRMS *data = (azaRMS*)dsp;

	if (flags & AZA_DSP_PROCESS_FLAG_CUT) {
		azaRMSReset(data);
	}

	err = azaCheckBuffersForDSPProcess(dst, src, /* sameFrameCount: */ true, /* sameChannelCount: */ false);
	if AZA_UNLIKELY(err) return err;

	if (dst->channelLayout.count != 1) {
		if (dst->channelLayout.count != src->channelLayout.count) {
			AZA_LOG_ERR("azaRMS Error: Expected dst to have either 1 channel or the same number as src, but dst had %u channels and src had %u channels.\n", (uint32_t)dst->channelLayout.count, (uint32_t)src->channelLayout.count);
			return AZA_ERROR_MISMATCHED_CHANNEL_COUNT;
		}
	}

	err = azaHandleRMSBuffer(data, dst->channelLayout.count);
	if AZA_UNLIKELY(err) return err;

	if (dst->channelLayout.count > data->header.prevChannelCountDst) {
		azaRMSResetChannels(data, data->header.prevChannelCountDst, dst->channelLayout.count - data->header.prevChannelCountDst);
	}
	data->header.prevChannelCountDst = dst->channelLayout.count;

	if (dst->channelLayout.count == 1 && src->channelLayout.count != 1) {
		// Combine channels
		azaRMSChannelData *channelData = &data->channelData[0];
		float *channelBuffer = &data->buffer[0];
		fp_azaOp op = data->config.combineOp ? data->config.combineOp : azaOpMax;
		for (size_t i = 0; i < src->frames; i++) {
			channelData->squaredSum -= channelBuffer[data->index];
			channelBuffer[data->index] = 0.0f;
			for (size_t c = 0; c < src->channelLayout.count; c++) {
				op(&channelBuffer[data->index], azaSqrf(src->pSamples[i * src->stride + c]));
			}
			channelData->squaredSum += channelBuffer[data->index];
			// Deal with potential rounding errors making sqrtf emit NaNs
			if (channelData->squaredSum < 0.0f) channelData->squaredSum = 0.0f;
			dst->pSamples[i * dst->stride] = sqrtf(channelData->squaredSum/(data->config.windowSamples * src->channelLayout.count));
			if (++data->index >= data->config.windowSamples)
				data->index = 0;
		}
	} else {
		// Individual channels
		for (uint32_t c = 0; c < dst->channelLayout.count; c++) {
			azaRMSChannelData *channelData = &data->channelData[c];
			float *channelBuffer = &data->buffer[data->config.windowSamples * c];
			for (size_t i = 0; i < src->frames; i++) {
				channelData->squaredSum -= channelBuffer[data->index];
				channelBuffer[data->index] = azaSqrf(src->pSamples[i * src->stride + c]);
				channelData->squaredSum += channelBuffer[data->index];
				// Deal with potential rounding errors making sqrtf emit NaNs
				if (channelData->squaredSum < 0.0f) channelData->squaredSum = 0.0f;
				dst->pSamples[i * dst->stride] = sqrtf(channelData->squaredSum/(data->config.windowSamples * src->channelLayout.count));
				if (++data->index >= data->config.windowSamples)
					data->index = 0;
			}
		}
	}
	return AZA_SUCCESS;
}