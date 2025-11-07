/*
	File: azaMeters.c
	Author: Philip Haynes
*/

#include "azaMeters.h"

#include "../math.h"

#include "../gui/gui.h"

void azaMetersReset(azaMeters *data) {
	*data = (azaMeters) {0};
}

void azaMetersResetChannels(azaMeters *data, uint32_t firstChannel, uint32_t channelCount) {
	memset(&data->rmsSquaredAvg[firstChannel], 0, sizeof(float) * channelCount);
	memset(&data->peaks[firstChannel], 0, sizeof(float) * channelCount);
	memset(&data->peaksShortTerm[firstChannel], 0, sizeof(float) * channelCount);
}

void azaMetersUpdate(azaMeters *data, azaBuffer *buffer, float inputAmp) {
	uint8_t channels = AZA_MIN((uint8_t)AZA_MAX_CHANNEL_POSITIONS, buffer->channelLayout.count);
	for (uint8_t c = data->activeMeters; c < channels; c++) {
		data->rmsSquaredAvg[c] = 0.0f;
		data->peaks[c] = 0.0f;
	}
	data->activeMeters = channels;
	for (uint8_t c = 0; c < channels; c++) {
		data->peaksShortTerm[c] = 0.0f;
		float rmsSquaredAvg = 0.0f;
		float peak = 0.0f;
		for (uint32_t i = 0; i < buffer->frames; i++) {
			float sample = buffer->pSamples[i * buffer->stride + c];
			rmsSquaredAvg += azaSqrf(sample);
			sample = azaAbsf(sample);
			peak = azaMaxf(peak, sample);
		}
		rmsSquaredAvg /= (float)buffer->frames;
		rmsSquaredAvg *= azaSqrf(inputAmp);
		peak *= inputAmp;
		data->rmsSquaredAvg[c] = azaLerpf(data->rmsSquaredAvg[c], rmsSquaredAvg, (float)buffer->frames / ((float)data->rmsFrames + (float)buffer->frames));
		data->peaks[c] = azaMaxf(data->peaks[c], peak);
		data->peaksShortTerm[c] = azaMaxf(data->peaksShortTerm[c], peak);
	}
	data->rmsFrames = AZA_MIN(data->rmsFrames + buffer->frames, 512);
}



// GUI



void azagDrawMeterBackground(azagRect bounds, int dbRange, int dbHeadroom) {
	azagDrawRectGradientV(bounds, azagThemeCurrent.meter.colorBGTop, azagThemeCurrent.meter.colorBGBot);
	azagRectShrinkAllV(&bounds, azagThemeCurrent.margin.y);
	azagDrawDBTicks(bounds, dbRange, dbHeadroom, azagThemeCurrent.meter.colorDBTick, azagThemeCurrent.meter.colorDBTickUnity);
}

int azagDrawMeters(azaMeters *meters, azagRect bounds, int dbRange, int dbHeadroom) {
	const int widthPeak = azagThemeCurrent.meter.channelDrawWidthPeak;
	const int widthRMS = azagThemeCurrent.meter.channelDrawWidthRMS;
	int channelDrawWidth = AZA_MAX(widthPeak, widthRMS);
	int usedWidth = channelDrawWidth * meters->activeMeters + azagThemeCurrent.meter.channelMargin * (meters->activeMeters+1);
	bounds.w = usedWidth;
	bool resetPeaks = azagMousePressedInRect(AZAG_MOUSE_BUTTON_LEFT, bounds);
	azagDrawMeterBackground(bounds, dbRange, dbHeadroom);
	azagRectShrinkMargin(&bounds, 0);
	bounds.w = channelDrawWidth;
	for (uint32_t c = 0; c < meters->activeMeters; c++) {
		float peakDB = aza_amp_to_dbf(meters->peaks[c]);
		int yOffset = azagDBToYOffsetClamped((float)dbHeadroom - peakDB, bounds.h, -2, (float)dbRange);
		azagColor peakColor = azagThemeCurrent.meter.colorPeak;
		if (meters->peaks[c] == 1.0f) {
			peakColor = azagThemeCurrent.meter.colorPeakUnity;
		} else if (meters->peaks[c] > 1.0f) {
			peakColor = azagThemeCurrent.meter.colorPeakOver;
		}
		azagDrawLine((azagPoint) {bounds.x, bounds.y + yOffset}, (azagPoint) {bounds.x+bounds.w, bounds.y + yOffset}, peakColor);
		if (true /* meters->processed */) {
			float rmsDB = aza_amp_to_dbf(sqrtf(meters->rmsSquaredAvg[c]));
			float peakShortTermDB = aza_amp_to_dbf(meters->peaksShortTerm[c]);
			yOffset = azagDBToYOffsetClamped((float)dbHeadroom - peakShortTermDB, bounds.h, 0, (float)dbRange);
			azagDrawRect((azagRect){
				bounds.x + (channelDrawWidth - widthPeak)/2,
				bounds.y + yOffset,
				widthPeak,
				bounds.h - yOffset
			}, azagThemeCurrent.meter.colorPeak);

			yOffset = azagDBToYOffsetClamped((float)dbHeadroom - rmsDB, bounds.h, 0, (float)dbRange);
			azagDrawRect((azagRect){
				bounds.x + (channelDrawWidth - widthRMS)/2,
				bounds.y + yOffset,
				widthRMS,
				bounds.h - yOffset
			}, azagThemeCurrent.meter.colorRMS);
			if (rmsDB > (float)dbHeadroom) {
				yOffset = azagDBToYOffsetClamped(rmsDB - (float)dbHeadroom, bounds.h, 0, (float)dbRange);
				azagDrawRect((azagRect){
					bounds.x + (channelDrawWidth - widthRMS)/2,
					bounds.y,
					widthRMS,
					yOffset
				}, azagThemeCurrent.meter.colorRMSOver);
			}
			if (peakShortTermDB > (float)dbHeadroom) {
				yOffset = azagDBToYOffsetClamped(peakShortTermDB - (float)dbHeadroom, bounds.h, 0, (float)dbRange);
				azagDrawRect((azagRect){
					bounds.x + (channelDrawWidth - widthPeak)/2,
					bounds.y,
					widthPeak,
					yOffset
				}, azagThemeCurrent.meter.colorPeakOver);
			}
		}
		bounds.x += bounds.w + azagThemeCurrent.margin.x;
		if (resetPeaks) {
			meters->peaks[c] = 0.0f;
		}
	}
	return usedWidth;
}