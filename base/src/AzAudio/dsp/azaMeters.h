/*
	File: azaMeters.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_AZAMETERS_H
#define AZAUDIO_AZAMETERS_H

#include "azaBuffer.h"

#include "../gui/types.h"

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



/*
	Draws the given azaMeters, with a width determined by theme and number of channels, and a height determined by bounds.
	dbRange is how many dB are physically represented across the height of the meter.
	dbHeadroom is how many dB above unity can be represented.
	- Actual range goes from dbHeadroom-dbRange to dbHeadroom
	returns used width
*/
int azagDrawMeters(azaMeters *meters, azagRect bounds, int dbRange, int dbHeadroom);
/*
	Just draws the background with the given dbRange and dbHeadroom, still themed after normal meters.
	You can use this as an easy way to make the background of custom meters.
	NOTE: The db ticks are spread across the bounds, shrunk vertically by the theme's margin on both ends.
	- This pairs nicely with `azagRectShrinkAllXY(&bounds, azagThemeCurrent.margin)` to get the proper bounds of the indicators.
*/
void azagDrawMeterBackground(azagRect bounds, int dbRange, int dbHeadroom);



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZAMETERS_H
