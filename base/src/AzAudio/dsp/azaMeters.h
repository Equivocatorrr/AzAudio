/*
	File: azaMeters.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_AZAMETERS_H
#define AZAUDIO_AZAMETERS_H

#include "azaBuffer.h"

#ifdef __cplusplus
extern "C" {
#endif



// Used for metering in the GUI
typedef struct azaMeters {
	// Up to AZA_MAX_CHANNEL_POSITIONS channels of RMS metering (only updated when mixer GUI is open)
	float rmsSquaredAvg[AZA_MAX_CHANNEL_POSITIONS];
	float peaks[AZA_MAX_CHANNEL_POSITIONS];
	float peaksShortTerm[AZA_MAX_CHANNEL_POSITIONS];
	// How many frames have been counted so far
	uint32_t rmsFrames;
	uint8_t activeMeters;
} azaMeters;
// Zeroes out the entire struct
void azaMetersReset(azaMeters *data);
// Zeroes out the specified channel range
void azaMetersResetChannels(azaMeters *data, uint32_t firstChannel, uint32_t channelCount);
// Will update the meters with the entirety of the buffer's contents
void azaMetersUpdate(azaMeters *data, azaBuffer *buffer, float inputAmp);



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZAMETERS_H
