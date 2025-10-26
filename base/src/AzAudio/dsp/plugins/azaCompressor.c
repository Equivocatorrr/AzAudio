/*
	File: azaCompressor.c
	Author: Philip Haynes
*/

#include "azaCompressor.h"

#include "../../AzAudio.h"
#include "../../math.h"
#include "../../error.h"

void azaCompressorInit(azaCompressor *data, azaCompressorConfig config) {
	data->header = azaCompressorHeader;
	data->config = config;
	azaRMSConfig rmsConfig = (azaRMSConfig) {
		.windowSamples = 128,
		.combineOp = azaOpMax
	};
	azaRMSInit(&data->rms, rmsConfig);
}

void azaCompressorDeinit(azaCompressor *data) {
	azaRMSDeinit(&data->rms);
}

void azaCompressorReset(azaCompressor *data) {
	azaMetersReset(&data->metersInput);
	azaMetersReset(&data->metersOutput);
	azaRMSReset(&data->rms);
}

void azaCompressorResetChannels(azaCompressor *data, uint32_t firstChannel, uint32_t channelCount) {
	azaMetersResetChannels(&data->metersInput, firstChannel, channelCount);
	azaMetersResetChannels(&data->metersOutput, firstChannel, channelCount);
	azaRMSResetChannels(&data->rms, firstChannel, channelCount);
}

azaCompressor* azaMakeCompressor(azaCompressorConfig config) {
	azaCompressor *result = aza_calloc(1, sizeof(azaCompressor));
	if (result) {
		azaCompressorInit(result, config);
	}
	return result;
}

void azaFreeCompressor(void *data) {
	azaCompressorDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultCompressor() {
	return (azaDSP*)azaMakeCompressor((azaCompressorConfig) {
		.threshold = -12.0f,
		.ratio = 10.0f,
		.attack_ms = 50.0f,
		.decay_ms = 200.0f,
	});
}

int azaCompressorProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	// Bypass handled by azaDSPProcess
	int err = AZA_SUCCESS;
	assert(dsp != NULL);
	azaCompressor *data = (azaCompressor*)dsp;

	if (flags & AZA_DSP_PROCESS_FLAG_CUT) {
		azaCompressorReset(data);
	}

	err = azaCheckBuffersForDSPProcess(dst, src, /* sameFrameCount: */ true, /* sameChannelCount: */ true);
	if AZA_UNLIKELY(err) return err;

	if (dst->channelLayout.count > data->header.prevChannelCountDst) {
		azaCompressorResetChannels(data, data->header.prevChannelCountDst, dst->channelLayout.count - data->header.prevChannelCountDst);
	}
	data->header.prevChannelCountDst = dst->channelLayout.count;

	float amountInput = aza_db_to_ampf(data->config.gainInput);
	if (data->header.selected) {
		azaMetersUpdate(&data->metersInput, src, amountInput);
	}

	azaBuffer rmsBuffer = azaPushSideBuffer(dst->frames, 0, 0, 1, dst->samplerate);
	err = azaRMSProcess(&data->rms, &rmsBuffer, src, flags);
	if AZA_UNLIKELY(err) return err;

	float t = (float)dst->samplerate / 1000.0f;
	float attackFactor = expf(-1.0f / (data->config.attack_ms * t));
	float decayFactor = expf(-1.0f / (data->config.decay_ms * t));
	float overgainFactor;
	if (data->config.ratio > 1.0f) {
		overgainFactor = (1.0f - 1.0f / data->config.ratio);
	} else if (data->config.ratio < 0.0f) {
		overgainFactor = -data->config.ratio;
	} else {
		overgainFactor = 0.0f;
	}
	data->minGainShort = 0.0f;
	float totalGain = data->config.gainOutput + data->config.gainInput;
	for (size_t i = 0; i < dst->frames; i++) {
		float rms = aza_amp_to_dbf(rmsBuffer.pSamples[i]) + data->config.gainInput;
		if (rms < -120.0f) rms = -120.0f;
		if (rms > data->attenuation) {
			data->attenuation = rms + attackFactor * (data->attenuation - rms);
		} else {
			data->attenuation = rms + decayFactor * (data->attenuation - rms);
		}
		float gain;
		if (data->attenuation > data->config.threshold) {
			gain = overgainFactor * (data->config.threshold - data->attenuation);
		} else {
			gain = 0.0f;
		}
		data->minGainShort = azaMinf(data->minGainShort, gain);
		float amp = aza_db_to_ampf(gain + totalGain);
		for (size_t c = 0; c < dst->channelLayout.count; c++) {
			size_t s = i * dst->stride + c;
			dst->pSamples[s] *= amp;
		}
	}
	data->minGain = azaMinf(data->minGain, data->minGainShort);
	azaPopSideBuffer();

	if (data->header.selected) {
		azaMetersUpdate(&data->metersOutput, src, 1.0f);
	}

	return AZA_SUCCESS;
}
