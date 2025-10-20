/*
	File: azaSampler.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_AZASAMPLER_H
#define AZAUDIO_AZASAMPLER_H

#include "../azaDSP.h"
#include "../azaMeters.h"
#include "../utility.h"
#include "../../backend/threads.h"

#ifdef __cplusplus
extern "C" {
#endif



#define AZAUDIO_SAMPLER_MAX_INSTANCES 128

typedef struct azaSamplerInstance {
	uint32_t id;
	int32_t frame;
	float fraction;
	bool reverse;
	byte _reserved[3]; // Explicit padding reserved for later.
	azaADSRInstance envelope;
	azaFollowerLinear speed;
	azaFollowerLinear volume;
} azaSamplerInstance;

typedef struct azaSamplerConfig {
	// buffer containing the sound we're sampling
	azaBuffer *buffer;
	// If speed changes this is how long it takes to lerp to the new value in ms
	float speedTransitionTimeMs;
	// If gain changes this is how long it takes to lerp to the new value in ms (lerp happens in amp space)
	float volumeTransitionTimeMs;
	// Whether instances loop around (must be true for pingpong to be respected)
	bool loop;
	// When we hit a loop point, this will make us reverse instead of wrapping around.
	bool pingpong;
	byte _reserved[6]; // Explicit padding reserved for later.
	// Start of the looping region in frames
	// If this value is >= buffer->frames, we treat this value as 0
	int32_t loopStart;
	// End of the looping region in frames
	// If this value is <= loopStart, we treat this value as buffer->frames
	int32_t loopEnd;
	azaADSRConfig envelope;
} azaSamplerConfig;

typedef struct azaSampler {
	azaDSP header;
	azaSamplerConfig config;
	azaMutex mutex;

	azaMeters metersOutput;

	azaSamplerInstance instances[AZAUDIO_SAMPLER_MAX_INSTANCES];
	uint32_t numInstances;
} azaSampler;

// initializes azaSampler in existing memory
void azaSamplerInit(azaSampler *data, azaSamplerConfig config);
// frees any additional memory that the azaSampler may have allocated
void azaSamplerDeinit(azaSampler *data);
// Resets state. May be called automatically.
void azaSamplerReset(azaSampler *data);
// Resets state for the specified channel range
void azaSamplerResetChannels(azaSampler *data, uint32_t firstChannel, uint32_t channelCount);

// Convenience function that allocates and inits an azaSampler for you
// May return NULL indicating an out-of-memory error
azaSampler* azaMakeSampler(azaSamplerConfig config);
// Frees an azaSampler that was created with azaMakeSampler
void azaFreeSampler(void *dsp);

azaDSP* azaMakeDefaultSampler();

int azaSamplerProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);




static const azaDSP azaSamplerHeader = {
	/* .size         = */ sizeof(azaSampler),
	/* .version      = */ 1,
	/* .owned, bypass, selected, prevChannelCountDst, prevChannelCountSrc */ false, false, false, 0, 0,
	/* ._reserved    = */ {0},
	/* .name         = */ "Sampler",
	/* fp_getSpecs   = */ NULL,
	/* fp_process    = */ azaSamplerProcess,
	/* fp_free       = */ azaFreeSampler,
	NULL, NULL,
};



// Adds an instance of the sound
// speed affects the rate of playback for this instance (pitch control where 1.0f is base speed)
// negative speed values will play the sound in reverse
// gainDB affects the volume for this instance
// returns the sound id, used for interacting with this instance later
uint32_t azaSamplerPlay(azaSampler *data, float speed, float gainDB);

// may return NULL, indicating the id wasn't found
azaSamplerInstance* azaSamplerGetInstance(azaSampler *data, uint32_t id);

static inline void azaSamplerSetSpeed(azaSampler *data, uint32_t id, float speed) {
	azaMutexLock(&data->mutex);
	azaSamplerInstance *instance = azaSamplerGetInstance(data, id);
	if (instance) {
		azaFollowerLinearSetTarget(&instance->speed, speed);
	}
	azaMutexUnlock(&data->mutex);
}

static inline void azaSamplerSetGain(azaSampler *data, uint32_t id, float gainDB) {
	azaMutexLock(&data->mutex);
	azaSamplerInstance *instance = azaSamplerGetInstance(data, id);
	if (instance) {
		azaFollowerLinearSetTarget(&instance->volume, aza_db_to_ampf(gainDB));
	}
	azaMutexUnlock(&data->mutex);
}

static inline float azaSamplerGetSpeedCurrent(azaSampler *data, uint32_t id) {
	azaMutexLock(&data->mutex);
	azaSamplerInstance *instance = azaSamplerGetInstance(data, id);
	float result = 0.0f;
	if (instance) {
		result = azaFollowerLinearGetValue(&instance->speed);
	}
	azaMutexUnlock(&data->mutex);
	return result;
}

static inline float azaSamplerGetGainCurrent(azaSampler *data, uint32_t id) {
	azaMutexLock(&data->mutex);
	azaSamplerInstance *instance = azaSamplerGetInstance(data, id);
	float result = 0.0f;
	if (instance) {
		result = aza_amp_to_dbf(azaFollowerLinearGetValue(&instance->volume));
	}
	azaMutexUnlock(&data->mutex);
	return result;
}

static inline float azaSamplerGetSpeedTarget(azaSampler *data, uint32_t id) {
	azaMutexLock(&data->mutex);
	azaSamplerInstance *instance = azaSamplerGetInstance(data, id);
	float result = 0.0f;
	if (instance) {
		result = instance->speed.end;
	}
	azaMutexUnlock(&data->mutex);
	return result;
}

static inline float azaSamplerGetGainTarget(azaSampler *data, uint32_t id) {
	azaMutexLock(&data->mutex);
	azaSamplerInstance *instance = azaSamplerGetInstance(data, id);
	float result = 0.0f;
	if (instance) {
		result = aza_amp_to_dbf(instance->volume.end);
	}
	azaMutexUnlock(&data->mutex);
	return result;
}

// Triggers the release of the given sound instance
void azaSamplerStop(azaSampler *data, uint32_t id);

void azaSamplerStopAll(azaSampler *data);



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZASAMPLER_H
