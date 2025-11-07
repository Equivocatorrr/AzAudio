/*
	File: azaCompressor.c
	Author: Philip Haynes
*/

#include "azaCompressor.h"

#include "../../AzAudio.h"
#include "../../math.h"
#include "../../error.h"

#include "../../gui/gui.h"

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



// GUI



static const int faderDBRange = 48;
static const int faderDBHeadroom = 12;
static const int thresholdDBRange = 48;
static const int thresholdDBHeadroom = 12;
static const int attenuationMeterDBRange = 24;

void azagDrawCompressor(void *dsp, azagRect bounds) {
	azaCompressor *data = dsp;
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
	yOffset = azagDBToYOffsetClamped(-data->minGainShort, attenuationRect.h, 0, (float)attenuationMeterDBRange);
	azagDrawRect((azagRect) {
		attenuationRect.x,
		attenuationRect.y,
		attenuationRect.w,
		yOffset
	}, azagThemeCurrent.colorAttenuation);
	yOffset = azagDBToYOffsetClamped(-data->minGain, attenuationRect.h, 0, (float)attenuationMeterDBRange);
	if (attenuationMouseover) {
		azagTooltipAdd(azaTextFormat("%+.1fdb", data->minGain), (azagPoint) {attenuationRect.x + attenuationRect.w + azagThemeCurrent.margin.x, attenuationRect.y + yOffset}, 0.0f, 0.5f);
	}
	azagDrawLine(
		(azagPoint) {attenuationRect.x, attenuationRect.y + yOffset},
		(azagPoint) {attenuationRect.x + attenuationRect.w, attenuationRect.y + yOffset},
		azagThemeCurrent.colorAttenuation
	);
	if (azagMousePressedInRect(AZAG_MOUSE_BUTTON_LEFT, attenuationRect)) {
		data->minGain = 1.0f;
	}

	usedWidth = azagDrawFader(bounds, &data->config.gainOutput, NULL, false, "Output Gain", faderDBRange, faderDBHeadroom);
	azagRectShrinkLeftMargin(&bounds, usedWidth);

	usedWidth = azagDrawMeters(&data->metersOutput, bounds, faderDBRange, faderDBHeadroom);
	// azagRectShrinkLeftMargin(&bounds, usedWidth);
}