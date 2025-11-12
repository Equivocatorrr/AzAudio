/*
	File: azaGate.c
	Author: Philip Haynes
*/

#include "azaGate.h"

#include "../../AzAudio.h"
#include "../../math.h"
#include "../../error.h"

#include "../../gui/gui.h"

void azaGateInit(azaGate *data, azaGateConfig config) {
	data->header = azaGateHeader;
	data->config = config;
	azaDSPChainInit(&data->activationEffects, 0);
	azaRMSInit(&data->rms, (azaRMSConfig) {
		.windowSamples = 128,
		.combineOp = azaOpMax
	});
}

void azaGateDeinit(azaGate *data) {
	azaDSPChainDeinit(&data->activationEffects);
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
	if (data->activationEffects.steps.count) {
		activationBuffer = azaPushSideBufferCopy(src);
		sideBuffersInUse++;
		err = azaDSPChainProcess(&data->activationEffects, &activationBuffer, &activationBuffer, flags);
		if AZA_UNLIKELY(err) {
			goto error;
		}
	} else {
		activationBuffer = *src;
	}

	err = azaRMSProcess(&data->rms, &rmsBuffer, &activationBuffer, flags);
	if AZA_UNLIKELY(err) {
		goto error;
	}
	float t = (float)src->samplerate / 1000.0f;
	float attackFactor = expf(-1.0f / (data->config.attack_ms * t));
	float decayFactor = expf(-1.0f / (data->config.decay_ms * t));
	float totalGain = data->config.gainOutput + data->config.gainInput;
	float undergainFactor = AZA_MAX(0.0f, data->config.ratio - 1.0f);
	for (size_t i = 0; i < dst->frames; i++) {
		float rms = aza_amp_to_dbf(rmsBuffer.pSamples[i]) + data->config.gainInput;
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
			gain = undergainFactor * (data->attenuation - data->config.threshold);
		}
		data->gain = gain;
		float amp = aza_db_to_ampf(gain + totalGain);
		for (uint8_t c = 0; c < dst->channelLayout.count; c++) {
			dst->pSamples[i * dst->stride + c] *= amp;
		}
	}

	if (data->header.selected) {
		azaMetersUpdate(&data->metersOutput, dst, 1.0f);
	}
error:
	azaPopSideBuffers(sideBuffersInUse);
	return err;
}



// GUI



static const int faderDBRange = 48;
static const int faderDBHeadroom = 12;
static const int thresholdDBRange = 48;
static const int thresholdDBHeadroom = 12;
static const int attenuationMeterDBRange = 48;

void azagDrawGate(void *dsp, azagRect bounds) {
	azaGate *data = dsp;
	int usedWidth;
	usedWidth = azagDrawFader(bounds, &data->config.gainInput, NULL, false, "Input Gain", faderDBRange, faderDBHeadroom);
	azagRectShrinkLeftMargin(&bounds, usedWidth);

	usedWidth = azagDrawMeters(&data->metersInput, bounds, faderDBRange, faderDBHeadroom);
	azagRectShrinkLeftMargin(&bounds, usedWidth);

	usedWidth = azagDrawFader(bounds, &data->config.threshold, NULL, false, "Threshold", thresholdDBRange, thresholdDBHeadroom);
	azagRectShrinkLeftMargin(&bounds, usedWidth);
	usedWidth = azagDrawSliderFloat(bounds, &data->config.ratio, 1.0f, 10.0f, 0.2f, 10.0f, "Ratio", "%.2f");
	azagRectShrinkLeftMargin(&bounds, usedWidth);
	usedWidth = azagDrawSliderFloatLog(bounds, &data->config.attack_ms, 1.0f, 1000.0f, 0.2f, 50.0f, "Attack", "%.1fms");
	azagRectShrinkLeftMargin(&bounds, usedWidth);
	usedWidth = azagDrawSliderFloatLog(bounds, &data->config.decay_ms, 1.0f, 1000.0f, 0.2f, 200.0f, "Release", "%.1fms");
	azagRectShrinkLeftMargin(&bounds, usedWidth);

	azagRect attenuationRect = {
		bounds.x,
		bounds.y,
		azagThemeCurrent.attenuationMeterWidth,
		bounds.h,
	};
	azagRectShrinkLeft(&bounds, attenuationRect.w + azagThemeCurrent.margin.x);
	bool attenuationMouseover = azagMouseInRect(attenuationRect);
	if (attenuationMouseover) {
		azagTooltipAdd("Attenuation", (azagPoint) {attenuationRect.x + attenuationRect.w/2, attenuationRect.y - azagThemeCurrent.margin.y}, 0.0f, 1.0f);
	}
	azagDrawMeterBackground(attenuationRect, attenuationMeterDBRange, 0);
	azagRectShrinkAll(&attenuationRect, azagThemeCurrent.margin.x);
	int yOffset;
	yOffset = azagDBToYOffsetClamped(-data->gain, attenuationRect.h, 0, (float)attenuationMeterDBRange);
	azagDrawRect((azagRect) {
		attenuationRect.x,
		attenuationRect.y,
		attenuationRect.w,
		yOffset
	}, azagThemeCurrent.colorAttenuation);
	azagDrawLine(
		(azagPoint) {attenuationRect.x, attenuationRect.y + yOffset},
		(azagPoint) {attenuationRect.x + attenuationRect.w, attenuationRect.y + yOffset},
		azagThemeCurrent.colorAttenuation
	);

	usedWidth = azagDrawFader(bounds, &data->config.gainOutput, NULL, false, "Output Gain", faderDBRange, faderDBHeadroom);
	azagRectShrinkLeftMargin(&bounds, usedWidth);

	usedWidth = azagDrawMeters(&data->metersOutput, bounds, faderDBRange, faderDBHeadroom);
	// azagRectShrinkLeftMargin(&bounds, usedWidth);
}