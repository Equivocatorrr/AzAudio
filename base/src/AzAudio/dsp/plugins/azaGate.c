/*
	File: azaGate.c
	Author: Philip Haynes
*/

#include "azaGate.h"

#include "../../AzAudio.h"
#include "../../math.h"
#include "../../error.h"

void azaGateInit(azaGate *data, azaGateConfig config) {
	data->header = azaGateHeader;
	data->config = config;
	azaRMSInit(&data->rms, (azaRMSConfig) {
		.windowSamples = 128,
		.combineOp = azaOpMax
	});
}

void azaGateDeinit(azaGate *data) {
	azaRMSDeinit(&data->rms);
}

void azaGateReset(azaGate *data) {
	azaMetersReset(&data->metersInput);
	azaMetersReset(&data->metersOutput);
	azaRMSReset(&data->rms);
}

void azaGateResetChannels(azaGate *data, uint32_t firstChannel, uint32_t channelCount) {
	azaMetersResetChannels(&data->metersInput, firstChannel, channelCount);
	azaMetersResetChannels(&data->metersOutput, firstChannel, channelCount);
	azaRMSResetChannels(&data->rms, firstChannel, channelCount);
}

azaGate* azaMakeGate(azaGateConfig config) {
	azaGate *result = aza_calloc(1, sizeof(azaGate));
	if (result) {
		azaGateInit(result, config);
	}
	return result;
}

void azaFreeGate(void *data) {
	azaGateDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultGate() {
	return (azaDSP*)azaMakeGate((azaGateConfig) {
		.threshold = -18.0f,
		.ratio = 10.0f,
		.attack_ms = 5.0f,
		.decay_ms = 100.0f,
		.gainInput = 0.0f,
		.gainOutput = 0.0f,
		.activationEffects = NULL,
	});
}

int azaGateProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	// Bypass handled by azaDSPProcess
	int err = AZA_SUCCESS;
	assert(dsp != NULL);
	azaGate *data = (azaGate*)dsp;

	if (flags & AZA_DSP_PROCESS_FLAG_CUT) {
		azaGateReset(data);
	}

	err = azaCheckBuffersForDSPProcess(dst, src, /* sameFrameCount: */ true, /* sameChannelCount: */ true);
	if AZA_UNLIKELY(err) return err;

	if (dst->channelLayout.count > data->header.prevChannelCountDst) {
		azaGateResetChannels(data, data->header.prevChannelCountDst, dst->channelLayout.count - data->header.prevChannelCountDst);
	}
	data->header.prevChannelCountDst = dst->channelLayout.count;

	float amountInput = aza_db_to_ampf(data->config.gainInput);
	if (data->header.selected) {
		azaMetersUpdate(&data->metersInput, src, amountInput);
	}

	azaBuffer rmsBuffer = azaPushSideBuffer(src->frames, 0, 0, 1, src->samplerate);
	uint8_t sideBuffersInUse = 1;
	azaBuffer activationBuffer;
	if (data->config.activationEffects) {
		activationBuffer = azaPushSideBufferCopy(src);
		sideBuffersInUse++;
		azaDSP *dspStart = data->config.activationEffects;
		azaDSP *dsp = dspStart;
		while (dsp) {
			err = azaDSPProcess(dsp, &activationBuffer, &activationBuffer, flags);
			if (err) {
				dsp->error = err;
			}
			dsp = dsp->pNext;
			if (dsp == dspStart) {
				return AZA_ERROR_MIXER_ROUTING_CYCLE;
			}
		}
	} else {
		activationBuffer = *src;
	}

	err = azaRMSProcess(&data->rms, &rmsBuffer, &activationBuffer, flags);
	if AZA_UNLIKELY(err) {
		azaPopSideBuffers(sideBuffersInUse);
		return err;
	}
	float t = (float)src->samplerate / 1000.0f;
	float attackFactor = expf(-1.0f / (data->config.attack_ms * t));
	float decayFactor = expf(-1.0f / (data->config.decay_ms * t));

	for (size_t i = 0; i < dst->frames; i++) {
		float rms = aza_amp_to_dbf(rmsBuffer.pSamples[i]);
		if (rms < -120.0f) rms = -120.0f;
		if (rms > data->config.threshold) {
			data->attenuation = rms + attackFactor * (data->attenuation - rms);
		} else {
			data->attenuation = rms + decayFactor * (data->attenuation - rms);
		}
		float gain;
		if (data->attenuation > data->config.threshold) {
			gain = 0.0f;
		} else {
			gain = -10.0f * (data->config.threshold - data->attenuation);
		}
		data->gain = gain;
		float amp = aza_db_to_ampf(gain);
		for (uint8_t c = 0; c < dst->channelLayout.count; c++) {
			dst->pSamples[i * dst->stride + c] *= amp;
		}
	}

	if (data->header.selected) {
		azaMetersUpdate(&data->metersOutput, dst, 1.0f);
	}
error:
	azaPopSideBuffers(sideBuffersInUse);
	return AZA_SUCCESS;
}
