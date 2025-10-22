/*
	File: azaCubicLimiter.c
	Author: Philip Haynes
*/

#include "azaCubicLimiter.h"

#include "../../AzAudio.h"
#include "../../math.h"
#include "../../error.h"

static float azaCubicLimiterSample(float sample) {
	sample = azaClampf(sample, -1.0f, 1.0f);
	sample = 1.5f * sample - 0.5f * sample * sample * sample;
	return sample;
}

void azaCubicLimiterInit(azaCubicLimiter *data, azaCubicLimiterConfig config) {
	data->header = azaCubicLimiterHeader;
	data->config = config;
	azaCubicLimiterReset(data);
}

void azaCubicLimiterDeinit(azaCubicLimiter *data) {
	// We good
}

void azaCubicLimiterReset(azaCubicLimiter *data) {
	azaMetersReset(&data->metersInput);
	azaMetersReset(&data->metersOutput);
}

void azaCubicLimiterResetChannels(azaCubicLimiter *data, uint32_t firstChannel, uint32_t channelCount) {
	azaMetersResetChannels(&data->metersInput, firstChannel, channelCount);
	azaMetersResetChannels(&data->metersOutput, firstChannel, channelCount);
}

azaCubicLimiter* azaMakeCubicLimiter(azaCubicLimiterConfig config) {
	azaCubicLimiter *result = aza_calloc(1, sizeof(azaCubicLimiter));
	if (result) {
		azaCubicLimiterInit(result, config);
	}
	return result;
}

void azaFreeCubicLimiter(void *dsp) {
	aza_free(dsp);
}

azaDSP* azaMakeDefaultCubicLimiter() {
	return (azaDSP*)azaMakeCubicLimiter((azaCubicLimiterConfig) {
		.gainInput = 0.0f,
		.gainOutput = 0.0f,
		.linkGain = false,
	});
}

int azaCubicLimiterProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	// Bypass handled by azaDSPProcess
	int err = AZA_SUCCESS;
	assert(dsp != NULL);
	azaCubicLimiter *data = (azaCubicLimiter*)dsp;

	if (flags & AZA_DSP_PROCESS_FLAG_CUT) {
		azaCubicLimiterReset(data);
	}

	err = azaCheckBuffersForDSPProcess(dst, src, /* sameFrameCount: */ true, /* sameChannelCount: */ true);
	if AZA_UNLIKELY(err) return err;

	// subtract the implicit gain caused by the slope at 0 being 1.5
	float amountInput = aza_db_to_ampf(data->config.gainInput - 3.5218251811136247f);
	float amountOutput = aza_db_to_ampf(data->config.gainOutput);

	if (data->header.selected) {
		azaMetersUpdate(&data->metersInput, src, amountInput);
	}

	for (size_t i = 0; i < dst->frames; i++) {
		for (size_t c = 0; c < dst->channelLayout.count; c++) {
			dst->pSamples[i * dst->stride + c] = amountOutput * azaCubicLimiterSample(amountInput * src->pSamples[i * src->stride + c]);
		}
	}

	if (data->header.selected) {
		azaMetersUpdate(&data->metersOutput, src, 1.0f);
	}
	return AZA_SUCCESS;
}