/*
	File: azaLookaheadLimiter.c
	Author: Philip Haynes
*/

#include "azaLookaheadLimiter.h"

#include "../../AzAudio.h"
#include "../../math.h"
#include "../../error.h"
#include "../../gui/gui.h"
#include "../../mixer.h"



static const azaDSPFuncs azaLookaheadLimiterFuncs = {
	.fp_makeDefault = azaLookaheadLimiterMakeDefault,
	.fp_makeDuplicate = azaLookaheadLimiterMakeDuplicate,
	.fp_copyConfig = azaLookaheadLimiterCopyConfig,
	.fp_getSpecs = azaLookaheadLimiterGetSpecs,
	.fp_process = azaLookaheadLimiterProcess,
	.fp_free = azaLookaheadLimiterFree,
	.fp_draw = azaLookaheadLimiterDraw,
};

const azaDSP azaLookaheadLimiterHeader = {
	.header =  {
		.size    = sizeof(azaLookaheadLimiter),
		.version = 1,
		.owned   = false,
		.bypass  = false,
	},
	.processMetadata = { 0 }, // ZII
	.guiMetadata = {
		.name             = "Lookahead Limiter",
		.selected         = 0,
		.drawTargetWidth  = 0.0f,
		.drawCurrentWidth = 0.0f,
	},
	.pFuncs = &azaLookaheadLimiterFuncs,
};

void azaLookaheadLimiterInit(azaLookaheadLimiter *data, azaLookaheadLimiterConfig config) {
	data->dsp = azaLookaheadLimiterHeader;
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

azaLookaheadLimiter* azaLookaheadLimiterMake(azaLookaheadLimiterConfig config) {
	azaLookaheadLimiter *result = aza_calloc(1, sizeof(azaLookaheadLimiter));
	if (result) {
		azaLookaheadLimiterInit(result, config);
	}
	return result;
}

void azaLookaheadLimiterFree(azaDSP *dsp) {
	azaLookaheadLimiterDeinit((azaLookaheadLimiter*)dsp);
	aza_free(dsp);
}

azaDSP* azaLookaheadLimiterMakeDefault() {
	return (azaDSP*)azaLookaheadLimiterMake((azaLookaheadLimiterConfig) {
		.gainInput = 0.0f,
		.gainOutput = 0.0f,
	});
}

azaDSP* azaLookaheadLimiterMakeDuplicate(azaDSP *src) {
	azaLookaheadLimiter *data = (azaLookaheadLimiter*)src;
	return (azaDSP*)azaLookaheadLimiterMake(data->config);
}

int azaLookaheadLimiterCopyConfig(azaDSP *dst, azaDSP *src) {
	azaLookaheadLimiter *dataDst = (azaLookaheadLimiter*)dst;
	azaLookaheadLimiter *dataSrc = (azaLookaheadLimiter*)src;
	dataDst->config = dataSrc->config;
	return AZA_SUCCESS;
}

int azaLookaheadLimiterProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	// Bypass handled by azaDSPProcess
	int err = AZA_SUCCESS;
	assert(dsp != NULL);
	azaLookaheadLimiter *data = (azaLookaheadLimiter*)dsp;

	if (flags & AZA_DSP_PROCESS_FLAG_CUT) {
		azaLookaheadLimiterReset(data);
	}

	err = azaCheckBuffersForDSPProcess(dst, src, /* sameFrameCount: */ true, /* sameChannelCount: */ true);
	if AZA_UNLIKELY(err) return err;

	if (dst->channelLayout.count > data->dsp.processMetadata.prevChannelCountDst) {
		azaLookaheadLimiterResetChannels(data, data->dsp.processMetadata.prevChannelCountDst, dst->channelLayout.count - data->dsp.processMetadata.prevChannelCountDst);
	}
	data->dsp.processMetadata.prevChannelCountDst = dst->channelLayout.count;

	float amountInput = aza_db_to_ampf(data->config.gainInput);
	float amountOutput = aza_db_to_ampf(data->config.gainOutput);
	if (azaMixerGUIDSPIsSelected(dsp)) {
		azaMetersUpdate(&data->metersInput, src, amountInput);
	}
	// TODO: There's some odd behavior where CPU usage jumps the instant there's any attenuation and never drops again. Pls investigate!
	azaBuffer gainBuffer;
	gainBuffer = azaPushSideBufferZero(dst->frames, dst->leadingFrames, dst->trailingFrames, 1, dst->samplerate);
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
	if (azaMixerGUIDSPIsSelected(dsp)) {
		azaMetersUpdate(&data->metersOutput, dst, 1.0f);
	}
	data->index = index;
	azaPopSideBuffer();
	return AZA_SUCCESS;
}

azaDSPSpecs azaLookaheadLimiterGetSpecs(azaDSP *dsp, uint32_t samplerate) {
	return (azaDSPSpecs) {
		.latencyFrames = AZAUDIO_LOOKAHEAD_SAMPLES,
	};
}



// GUI



static const float faderDBRange = 48.0f;
static const float faderDBHeadroom = 12.0f;
static const float attenuationMeterDBRange = 12.0f;

void azaLookaheadLimiterDraw(azaDSP *dsp, azagRect bounds) {
	azaLookaheadLimiter *data = (azaLookaheadLimiter*)dsp;
	float boundsStartX = bounds.x;
	float faderWidth = azagDrawFader(bounds, &data->config.gainInput, NULL, false, "Input Gain", faderDBRange, faderDBHeadroom);
	azagRectShrinkLeftMargin(&bounds, faderWidth);
	float metersWidth = azagDrawMeters(&data->metersInput, bounds, faderDBRange, faderDBHeadroom);
	azagRectShrinkLeftMargin(&bounds, metersWidth);

	azagRect attenuationRect = {
		bounds.x,
		bounds.y,
		azagThemeCurrent.attenuationMeterWidth,
		bounds.h,
	};
	azagRectShrinkLeftMargin(&bounds, attenuationRect.w);
	bool attenuationMouseover = azagMouseInRect(attenuationRect);
	if (attenuationMouseover) {
		azagTooltipAdd("Attenuation", (azaVec2) {attenuationRect.x + attenuationRect.w/2, attenuationRect.y - azagThemeCurrent.margin.y}, (azaVec2) { 0.5f, 1.0f
	});
	}
	azagDrawMeterBackground(attenuationRect, attenuationMeterDBRange, 0);
	azagRectShrinkAllXY(&attenuationRect, azagThemeCurrent.margin);
	float yOffset;
	yOffset = azagDBToYOffsetClamped(-aza_amp_to_dbf(data->minAmpShort), attenuationRect.h, 0, (float)attenuationMeterDBRange);
	azagDrawRect((azagRect) {
		attenuationRect.x,
		attenuationRect.y,
		attenuationRect.w,
		yOffset
	}, azagThemeCurrent.colorAttenuation);
	float attenuationPeakDB = aza_amp_to_dbf(data->minAmp);
	yOffset = azagDBToYOffsetClamped(-attenuationPeakDB, attenuationRect.h, 0, (float)attenuationMeterDBRange);
	if (attenuationMouseover) {
		azagTooltipAdd(azaTextFormat("%+.1fdb", attenuationPeakDB), (azaVec2) {
			attenuationRect.x + attenuationRect.w + azagThemeCurrent.margin.x,
			attenuationRect.y + yOffset
		}, (azaVec2) { 0.0f, 0.5f });
	}
	azagDrawLine(
		(azaVec2) {attenuationRect.x,                     attenuationRect.y + yOffset},
		(azaVec2) {attenuationRect.x + attenuationRect.w, attenuationRect.y + yOffset},
		azagThemeCurrent.colorAttenuation
	);
	if (azagMousePressedInRect(AZAG_MOUSE_BUTTON_LEFT, attenuationRect)) {
		data->minAmp = 1.0f;
	}
	data->minAmpShort = 1.0f;

	azagDrawFader(bounds, &data->config.gainOutput, NULL, false, "Output Gain", faderDBRange, faderDBHeadroom);
	azagRectShrinkLeftMargin(&bounds, faderWidth);
	azagDrawMeters(&data->metersOutput, bounds, faderDBRange, faderDBHeadroom);
	azagRectShrinkLeftMargin(&bounds, metersWidth);
	float totalWidth = bounds.x - boundsStartX + azagThemeCurrent.margin.x;
	data->dsp.guiMetadata.drawTargetWidth = totalWidth;
}