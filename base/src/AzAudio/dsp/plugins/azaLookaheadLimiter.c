/*
	File: azaLookaheadLimiter.c
	Author: Philip Haynes
*/

#include "azaLookaheadLimiter.h"

#include "../../AzAudio.h"
#include "../../math.h"
#include "../../error.h"



void azaLookaheadLimiterInit(azaLookaheadLimiter *data, azaLookaheadLimiterConfig config) {
	data->header = azaLookaheadLimiterHeader;
	data->config = config;
	azaLookaheadLimiterReset(data);
}

void azaLookaheadLimiterDeinit(azaLookaheadLimiter *data) {
	// Nah, we good
}

void azaLookaheadLimiterReset(azaLookaheadLimiter *data) {
	azaMetersReset(&data->metersInput);
	azaMetersReset(&data->metersOutput);
	data->minAmp = 1.0f;
	data->minAmpShort = 1.0f;
	memset(data->peakBuffer, 0, sizeof(data->peakBuffer));
	data->index = 0;
	data->cooldown = 0;
	data->sum = 1.0f;
	data->slope = 0.0f;
	memset(data->channelData, 0, sizeof(data->channelData));
}

void azaLookaheadLimiterResetChannels(azaLookaheadLimiter *data, uint32_t firstChannel, uint32_t channelCount) {
	azaMetersResetChannels(&data->metersInput, firstChannel, channelCount);
	azaMetersResetChannels(&data->metersOutput, firstChannel, channelCount);
	memset(data->channelData + firstChannel, 0, sizeof(data->channelData[0]) * channelCount);
}

azaLookaheadLimiter* azaMakeLookaheadLimiter(azaLookaheadLimiterConfig config) {
	azaLookaheadLimiter *result = aza_calloc(1, sizeof(azaLookaheadLimiter));
	if (result) {
		azaLookaheadLimiterInit(result, config);
	}
	return result;
}

void azaFreeLookaheadLimiter(void *data) {
	azaLookaheadLimiterDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultLookaheadLimiter() {
	return (azaDSP*)azaMakeLookaheadLimiter((azaLookaheadLimiterConfig) {
		.gainInput = 0.0f,
		.gainOutput = 0.0f,
	});
}

int azaLookaheadLimiterProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	// Bypass and chaining handled by azaDSPProcess
	int err = AZA_SUCCESS;
	assert(dsp != NULL);
	azaLookaheadLimiter *data = (azaLookaheadLimiter*)dsp;

	if (flags & AZA_DSP_PROCESS_FLAG_CUT) {
		azaLookaheadLimiterReset(data);
	}

	err = azaCheckBuffersForDSPProcess(dst, src, /* sameFrameCount: */ true, /* sameChannelCount: */ true);
	if AZA_UNLIKELY(err) return err;

	if (dst->channelLayout.count > data->header.prevChannelCountDst) {
		azaLookaheadLimiterResetChannels(data, data->header.prevChannelCountDst, dst->channelLayout.count - data->header.prevChannelCountDst);
	}
	data->header.prevChannelCountDst = dst->channelLayout.count;

	float amountInput = aza_db_to_ampf(data->config.gainInput);
	float amountOutput = aza_db_to_ampf(data->config.gainOutput);
	if (data->header.selected) {
		azaMetersUpdate(&data->metersInput, src, amountInput);
	}
	// TODO: There's some odd behavior where CPU usage jumps the instant there's any attenuation and never drops again. Pls investigate!
	azaBuffer gainBuffer;
	gainBuffer = azaPushSideBufferZero(dst->frames, dst->framesLeading, dst->framesTrailing, 1, dst->samplerate);
	// TODO: It may be desirable to prevent the subwoofer channel from affecting the rest, and it may want its own independent limiter.
	int index = data->index;
	// Do all the gain calculations and put them into gainBuffer
	for (uint32_t i = 0; i < dst->frames; i++) {
		for (uint8_t c = 0; c < src->channelLayout.count; c++) {
			float sample = azaAbsf(src->pSamples[i * src->stride + c]);
			gainBuffer.pSamples[i] = azaMaxf(sample, gainBuffer.pSamples[i]);
		}
		float peak = azaMaxf(gainBuffer.pSamples[i] * amountInput, 1.0f);
		data->peakBuffer[index] = peak;
		index = (index+1)%AZAUDIO_LOOKAHEAD_SAMPLES;
		float slope = (1.0f / peak - data->sum) / AZAUDIO_LOOKAHEAD_SAMPLES;
		if (slope < data->slope) {
			data->slope = slope;
			data->cooldown = AZAUDIO_LOOKAHEAD_SAMPLES;
		} else if (data->cooldown == 0 && data->sum < 1.0f) {
			data->slope = (1.0f - data->sum) / (AZAUDIO_LOOKAHEAD_SAMPLES * 5.0f);
			for (int index2 = 0; index2 < AZAUDIO_LOOKAHEAD_SAMPLES; index2++) {
				float peak2 = data->peakBuffer[(index+index2)%AZAUDIO_LOOKAHEAD_SAMPLES];
				float slope2 = (1.0f / peak2 - data->sum) / (float)(index2+1);
				if (slope2 < data->slope) {
					data->slope = slope2;
					data->cooldown = index2+1;
				}
			}
		} else if (data->cooldown > 0) {
			data->cooldown -= 1;
		}
		data->sum += data->slope;
		data->minAmpShort = azaMinf(data->minAmpShort, data->sum);
		if (data->sum > 1.0f) {
			data->slope = 0.0f;
			data->sum = 1.0f;
		}
		gainBuffer.pSamples[i] = data->sum;
	}
	data->minAmp = azaMinf(data->minAmp, data->minAmpShort);
	// Apply the gain from gainBuffer to all the channels
	for (uint8_t c = 0; c < dst->channelLayout.count; c++) {
		azaLookaheadLimiterChannelData *channelData = &data->channelData[c];
		index = data->index;

		for (uint32_t i = 0; i < dst->frames; i++) {
			channelData->valBuffer[index] = src->pSamples[i * src->stride + c];
			index = (index+1)%AZAUDIO_LOOKAHEAD_SAMPLES;
			float out = azaClampf(channelData->valBuffer[index] * gainBuffer.pSamples[i] * amountInput, -1.0f, 1.0f);
			dst->pSamples[i * dst->stride + c] = out * amountOutput;
		}
	}
	if (data->header.selected) {
		azaMetersUpdate(&data->metersOutput, dst, 1.0f);
	}
	data->index = index;
	azaPopSideBuffer();
	return AZA_SUCCESS;
}

azaDSPSpecs azaLookaheadLimiterGetSpecs(void *dsp, uint32_t samplerate) {
	return (azaDSPSpecs) {
		.latencyFrames = AZAUDIO_LOOKAHEAD_SAMPLES,
	};
}