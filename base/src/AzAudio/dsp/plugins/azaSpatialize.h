/*
	File: azaSpatialize.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_AZASPATIALIZE_H
#define AZAUDIO_AZASPATIALIZE_H

#include "../utility.h"
#include "../azaDSP.h"
#include "azaDelayDynamic.h"
#include "azaFilter.h"

#ifdef __cplusplus
extern "C" {
#endif


/*
struct azaSpatialize;
// This gets called for the beginning and end of each sound source, once per ear (in this case an ear is an output channel with an associated direction, determined by azaChannelLayout.position[dstChannel]).
// spatialize is the currently processing azaSpatialize.
// srcChannel is which channel from src is being processed
// srcPos is where the sound source is located relative to the listener's ear (as defined by azaWorld)
// srcNormal is the normal vector of the sound source (optional, may be good for speakers with directionality).
// channelNormal is the direction the ear is facing.
// returns the amplitude of a sound, attenuated as you like based on the given parameters.
typedef float (*fp_azaSpatializeGetAmp)(struct azaSpatialize *spatialize, uint8_t srcChannel, azaVec3 srcPos, azaVec3 srcNormal, azaVec3 channelNormal);
//
float azaSpatializeGetAmpOne(struct azaSpatialize *spatialize, uint8_t srcChannel, azaVec3 srcPos, azaVec3 srcNormal, azaVec3 channelNormal);

typedef struct azaSpatializeChannelConfig {
	// Targets for the followers
	struct {
		azaVec3 position;
		azaVec3 normal;
	} target;
	// Parameters used for fp_getAmp
	float params[2];
} azaSpatializeChannelConfig;
*/
typedef struct azaSpatializeChannelConfig {
	// Targets for the followers
	struct {
		azaVec3 position;
		float amplitude;
	} target;
} azaSpatializeChannelConfig;

typedef struct azaSpatializeChannelData {
	azaFollowerLinear3D position;
	azaFollowerLinear3D normal;
	azaFollowerLinear amplitude;
	azaFilter filter;
	azaDelayDynamic delay;
} azaSpatializeChannelData;

typedef struct azaSpatializeConfig {
	// if world is NULL, it will use azaWorldDefault
	const azaWorld *world;
	// If this is NULL, we use the channels[c].targetAmplitude, else this gives us
	// fp_azaSpatializeGetAmp fp_getAmp;
	// This can point to whatever you want. Useful for fp_getAmp
	// void *userdata;
	// If true, we take into account the total distance to calculate the delay time, which simulates the doppler effect.
	bool doDoppler;
	// If true, we apply a low-pass filter with a cutoff that depends on distance, mimicking the effect of lower frequencies traveling further.
	bool doFilter;
	// If true, we calculate separate delays per output channel, positioning each spatially based on the channel layout, at a distance of earDistance.
	bool usePerChannelDelay;
	// If true, we calculate separate filter cutoffs per channel.
	bool usePerChannelFilter;

	// We can spatialize multiple channels at once, each with their own positions. This is how many we want to use.
	uint8_t numSrcChannelsActive;

	byte _reserved[7];

	// How long it takes to reach the follower target in ms
	float targetFollowTime_ms;
	// Maximum delay time in ms for ADVANCED mode. If this is zero, we'll use some default that should work for most reasonable distances.
	float delayMax_ms;
	// In ADVANCED mode, this specifies how far each channel is from the origin in their respective directions. Used to calculate per-channel delays. If this is zero, it will default to 0.085f (half of the average human head width).
	float earDistance;
	// Target positions for
	azaSpatializeChannelConfig channels[AZA_MAX_CHANNEL_POSITIONS];
} azaSpatializeConfig;

typedef struct azaSpatializeEvent {
	azaQueueEntry header;
	azaSpatializeChannelConfig newConfig[AZA_MAX_CHANNEL_POSITIONS];
} azaSpatializeEvent;

typedef struct azaSpatialize {
	azaDSP header;
	azaSpatializeConfig config;
	// TODO: azaQueue eventQueue;

	azaMeters metersInput;
	azaMeters metersOutput;

	azaSpatializeChannelData channelData[AZA_MAX_CHANNEL_POSITIONS];
} azaSpatialize;

// initializes azaSpatialize in existing memory
void azaSpatializeInit(azaSpatialize *data, azaSpatializeConfig config);
// frees any additional memory that the azaSpatialize may have allocated
void azaSpatializeDeinit(azaSpatialize *data);
// Resets state. May be called automatically.
void azaSpatializeReset(azaSpatialize *data);
// Resets state for the specified channel range
void azaSpatializeResetChannels(azaSpatialize *data, uint32_t firstChannel, uint32_t channelCount);

// Convenience function that allocates and inits an azaSpatialize for you
// May return NULL indicating an out-of-memory error
azaSpatialize* azaMakeSpatialize(azaSpatializeConfig config);
// Frees an azaSpatialize that was created with azaMakeSpatialize
void azaFreeSpatialize(void *dsp);

azaDSP* azaMakeDefaultSpatialize();

int azaSpatializeProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);

// azaDelayDynamic's sampling kernel causes there to be a minimum latency requirement, so we'll report that here
azaDSPSpecs azaSpatializeGetSpecs(void *dsp, uint32_t samplerate);



static const azaDSP azaSpatializeHeader = {
	/* .size         = */ sizeof(azaSpatialize),
	/* .version      = */ 1,
	/* .owned, bypass, selected, prevChannelCountDst, prevChannelCountSrc */ false, false, false, 0, 0,
	/* ._reserved    = */ {0},
	/* .error        = */ 0,
	/* .name         = */ "Spatialize",
	/* fp_getSpecs   = */ azaSpatializeGetSpecs,
	/* fp_process    = */ azaSpatializeProcess,
	/* fp_free       = */ azaFreeSpatialize,
	NULL, NULL,
};



// Utilities



// Sets up targets and followers such that it will ramp from start to end perfectly in the span of frames at the given samplerate.
// Also sets data->config.numSrcChannelsActive to numChannels.
// Useful for directly calling azaSpatializeProcess in another process callback.
// Expects start and end to be arrays of length numChannels.
void azaSpatializeSetRamps(azaSpatialize *data, uint8_t numChannels, azaSpatializeChannelConfig start[], azaSpatializeChannelConfig end[], uint32_t frames, uint32_t samplerate);



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZASPATIALIZE_H
