/*
	File: azaRMS.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_AZARMS_H
#define AZAUDIO_AZARMS_H

#include "../azaDSP.h"

#ifdef __cplusplus
extern "C" {
#endif



extern const azaDSP azaRMSHeader;

typedef struct azaRMSConfig {
	uint32_t windowSamples;
	uint32_t _reserved; // Explicit padding bytes, reserved for later use.
	// If dst is 1 channel, this is used to combine all the channel values into a single RMS value per frame. If left NULL, defaults to azaOpMax
	fp_azaOp combineOp;
} azaRMSConfig;

typedef struct azaRMSChannelData {
	float squaredSum;
} azaRMSChannelData;

typedef struct azaRMS {
	azaDSP dsp;
	azaRMSConfig config;
	uint32_t index;
	uint32_t bufferCap;
	float *buffer;
	azaRMSChannelData channelData[AZA_MAX_CHANNEL_POSITIONS];
} azaRMS;

// initializes azaRMS in existing memory
void azaRMSInit(azaRMS *data, azaRMSConfig config);
// frees any additional memory that the azaRMS may have allocated
void azaRMSDeinit(azaRMS *data);
// Resets the running tally
void azaRMSReset(azaRMS *data);
// Resets the specified channel range
void azaRMSResetChannels(azaRMS *data, uint32_t firstChannel, uint32_t channelCount);

// Convenience function that allocates and inits an azaRMS for you
// May return NULL indicating an out-of-memory error
azaRMS* azaRMSMake(azaRMSConfig config);
// Frees an azaRMS that was created with azaRMSMake
void azaRMSFree(azaDSP *dsp);

azaDSP* azaRMSMakeDefault();
azaDSP* azaRMSMakeDuplicate(azaDSP *src);
int azaRMSCopyConfig(azaDSP *dst, azaDSP *src);

int azaRMSProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZARMS_H