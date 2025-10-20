/*
	File: azaMeters.c
	Author: Philip Haynes
*/

#include "azaMeters.h"

#include "../math.h"

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
