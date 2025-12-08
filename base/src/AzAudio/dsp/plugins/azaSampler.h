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



extern const azaDSP azaSamplerHeader;
// TODO: Probably make this dynamic
#define AZAUDIO_SAMPLER_MAX_INSTANCES 16

typedef struct azaSamplerInstance {
	azaBuffer *buffer;
	uint32_t id;
	int32_t frame;
	float fraction;
	bool reverse;
	// Whether instances loop around (must be true for pingpong to be respected)
	bool loop;
	// When we hit a loop point, this will make us reverse instead of wrapping around.
	bool pingpong;
	aza_byte _reserved[9]; // Explicit padding reserved for later.
	// Start of the looping region in frames
	// If this value is >= buffer->frames, we treat this value as 0
	int32_t loopStart;
	// End of the looping region in frames
	// If this value is <= loopStart, we treat this value as buffer->frames
	int32_t loopEnd;
	azaADSR envelope;
	azaFollowerLinear speed;
	azaFollowerLinear volume;
} azaSamplerInstance;
static_assert(sizeof(azaSamplerInstance) == (sizeof(azaBuffer*) + 88), "Please update the expected size of azaSamplerInstance and remember to reserve padding explicitly.");

typedef struct azaSamplerConfig {
	// If speed changes this is how long it takes to lerp to the new value in ms
	float speedTransitionTimeMs;
	// If gain changes this is how long it takes to lerp to the new value in ms (lerp happens in amp space)
	float volumeTransitionTimeMs;
	aza_byte _reserved[8]; // Explicit padding reserved for later.
} azaSamplerConfig;
static_assert(sizeof(azaSamplerConfig) == (16), "Please update the expected size of azaSamplerConfig and remember to reserve padding explicitly.");

typedef struct azaSampler {
	azaDSP dsp;
	azaSamplerConfig config;
	azaMutex mutex;

	azaMeters metersOutput;

	azaSamplerInstance instances[AZAUDIO_SAMPLER_MAX_INSTANCES];
	uint32_t numInstances;
	aza_byte _reserved[4]; // Explicit padding reserved for later.
} azaSampler;
static_assert(sizeof(azaSampler) == (sizeof(azaDSP) + sizeof(azaSamplerConfig) + sizeof(azaMutex) + sizeof(azaMeters) + sizeof(azaSamplerInstance) * AZAUDIO_SAMPLER_MAX_INSTANCES + 8), "Please update the expected size of azaSampler and remember to reserve padding explicitly.");

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
azaSampler* azaSamplerMake(azaSamplerConfig config);
// Frees an azaSampler that was created with azaSamplerMake
void azaSamplerFree(azaDSP *dsp);

azaDSP* azaSamplerMakeDefault();
azaDSP* azaSamplerMakeDuplicate(azaDSP *src);
int azaSamplerCopyConfig(azaDSP *dst, azaDSP *src);

int azaSamplerProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);




// Adds an instance of the sound in buffer
// speed affects the rate of playback for this instance (pitch control where 1.0f is base speed)
// negative speed values will play the sound in reverse
// gainDB affects the volume for this instance
// envelope determines how the volume will change over time
// if loop is true, then the sound will loop using the given loop range
// if pingpong is true, then instead of a loop wrapping around to the opposite end of the loop range, it will reverse the sound at a loop range boundary.
// loopStart is the start of the looping region in frames
// If this value is >= buffer->frames, we treat this value as 0
// returns the sound id, used for interacting with this instance later
uint32_t azaSamplerPlayFull(azaSampler *data, azaBuffer *buffer, float speed, float gainDB, azaADSRConfig envelope, bool loop, bool pingpong, int32_t loopStart, int32_t loopEnd);

static inline uint32_t azaSamplerPlay(azaSampler *data, azaBuffer *buffer, float speed, float gainDB, azaADSRConfig envelope) {
	return azaSamplerPlayFull(data, buffer, speed, gainDB, envelope, false, false, 0, 0);
}
// Simplest kind of loop, just wraps around the entirety of buffer
static inline uint32_t azaSamplerLoop(azaSampler *data, azaBuffer *buffer, float speed, float gainDB, azaADSRConfig envelope) {
	return azaSamplerPlayFull(data, buffer, speed, gainDB, envelope, true, false, 0, 0);
}

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
