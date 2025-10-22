/*
	File: azaFilter.c
	Author: Philip Haynes
*/

#include "azaFilter.h"

#include "../../AzAudio.h"
#include "../../math.h"
#include "../../error.h"



const char *azaFilterKindString[] = {
	"High Pass",
	"Low Pass",
	"Band Pass",
};
static_assert(sizeof(azaFilterKindString) / sizeof(const char*) == AZA_FILTER_KIND_COUNT, "Pls update azaFilterKindString");



void azaFilterInit(azaFilter *data, azaFilterConfig config) {
	data->header = azaFilterHeader;
	data->config = config;
	azaFilterReset(data);
}

void azaFilterDeinit(azaFilter *data) {
	// We good
}

void azaFilterReset(azaFilter *data) {
	azaMetersReset(&data->metersInput);
	azaMetersReset(&data->metersOutput);
	memset(data->channelData, 0, sizeof(data->channelData));
}

void azaFilterResetChannels(azaFilter *data, uint32_t firstChannel, uint32_t channelCount) {
	azaMetersResetChannels(&data->metersInput, firstChannel, channelCount);
	azaMetersResetChannels(&data->metersOutput, firstChannel, channelCount);
	memset(data->channelData + firstChannel, 0, sizeof(data->channelData[0]) * channelCount);
}

azaFilter* azaMakeFilter(azaFilterConfig config) {
	azaFilter *result = aza_calloc(1, sizeof(azaFilter));
	if (result) {
		azaFilterInit(result, config);
	}
	return result;
}

void azaFreeFilter(void *data) {
	azaFilterDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultFilter() {
	return (azaDSP*)azaMakeFilter((azaFilterConfig) {
		.kind = AZA_FILTER_LOW_PASS,
		.poles = AZA_FILTER_12_DB,
		.frequency = 500.0f,
		.dryMix = 0.0f,
		.gainWet = 0.0f,
	});
}

int azaFilterProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	// Bypass handled by azaDSPProcess
	int err = AZA_SUCCESS;
	assert(dsp != NULL);
	azaFilter *data = (azaFilter*)dsp;

	if (flags & AZA_DSP_PROCESS_FLAG_CUT) {
		azaFilterReset(data);
	}

	err = azaCheckBuffersForDSPProcess(dst, src, /* sameFrameCount: */ true, /* sameChannelCount: */ true);
	if AZA_UNLIKELY(err) return err;

	if (dst->channelLayout.count > data->header.prevChannelCountDst) {
		azaFilterResetChannels(data, data->header.prevChannelCountDst, dst->channelLayout.count - data->header.prevChannelCountDst);
	}
	data->header.prevChannelCountDst = dst->channelLayout.count;

	float amountWet = azaClampf(1.0f - data->config.dryMix, 0.0f, 1.0f) * aza_db_to_ampf(data->config.gainWet);
	float amountDry = azaClampf(data->config.dryMix, 0.0f, 1.0f);

	if (data->header.selected) {
		azaMetersUpdate(&data->metersInput, src, 1.0f);
	}

	uint32_t poles = AZA_MIN(data->config.poles+1, AZAUDIO_FILTER_MAX_POLES);
	for (uint8_t c = 0; c < dst->channelLayout.count; c++) {
		azaFilterChannelData *channelData = &data->channelData[c];
		float channelFrequencyOverride = data->config.channelFrequencyOverride[c];
		float frequency = channelFrequencyOverride != 0.0f ? channelFrequencyOverride : data->config.frequency;
		float decay = azaClampf(expf(-AZA_TAU * (frequency / (float)dst->samplerate)), 0.0f, 1.0f);

		switch (data->config.kind) {
			case AZA_FILTER_HIGH_PASS: {
				for (uint32_t i = 0; i < dst->frames; i++) {
					// TODO: High pass seems to lose a lot of volume with lots of poles. Investigate if this is expected and how to handle it.
					float sample = src->pSamples[i * src->stride + c];
					channelData->outputs[0] = sample + decay * (channelData->outputs[0] - sample);
					sample -= channelData->outputs[0];
					for (uint32_t pole = 1; pole < poles; pole++) {
						channelData->outputs[pole] = sample + decay * (channelData->outputs[pole] - sample);
						sample -= channelData->outputs[pole];
					}
					dst->pSamples[i * dst->stride + c] = sample * amountWet + src->pSamples[i * src->stride + c] * amountDry;
				}
			} break;
			case AZA_FILTER_LOW_PASS: {
				for (uint32_t i = 0; i < dst->frames; i++) {
					float sample = src->pSamples[i * src->stride + c];
					channelData->outputs[0] = sample + decay * (channelData->outputs[0] - sample);
					for (uint32_t pole = 1; pole < poles; pole++) {
						channelData->outputs[pole] = channelData->outputs[pole-1] + decay * (channelData->outputs[pole] - channelData->outputs[pole-1]);
					}
					dst->pSamples[i * dst->stride + c] = channelData->outputs[poles-1] * amountWet + src->pSamples[i * src->stride + c] * amountDry;
				}
			} break;
			case AZA_FILTER_BAND_PASS: {
				for (uint32_t i = 0; i < dst->frames; i++) {
					float sample = src->pSamples[i * src->stride + c];
					for (uint32_t pole = 0; pole < poles; pole++) {
						// Low pass
						channelData->outputs[2*pole+0] = sample + decay * (channelData->outputs[2*pole+0] - sample);
						sample = channelData->outputs[2*pole+0];
						// High pass
						channelData->outputs[2*pole+1] = sample + decay * (channelData->outputs[2*pole+1] - sample);
						sample -= channelData->outputs[2*pole+1];
						// Compensate for the innate -3dB at the cutoff, done twice is -6dB, which is ~1/2 amp
						sample *= 2.0f;
					}
					dst->pSamples[i * dst->stride + c] = sample * amountWet + src->pSamples[i * src->stride + c] * amountDry;
				}
			} break;
			case AZA_FILTER_KIND_COUNT: break;
		}
	}

	if (data->header.selected) {
		azaMetersUpdate(&data->metersOutput, src, 1.0f);
	}

	return AZA_SUCCESS;
}
