/*
	File: utility.h
	Author: Philip Haynes
	Collection of utility functions for implementing plugin interfaces.
*/

#ifndef AZAUDIO_UTILITY_H
#define AZAUDIO_UTILITY_H

#include "../math.h"

#ifdef __cplusplus
extern "C" {
#endif



// Signed 32.32 fixed point time format that measures seconds.
// Can measure time 68 years in the past or future to a precision of 2^-32 divisions per second.
// That's about 89478 divisions per sample at 48kHz.
typedef union azaTime {
	int64_t time;
	struct {
		int32_t seconds;
		uint32_t frac;
	};
} azaTime;

static const azaTime azaTimeOneSecond = { (int64_t)1 << 32 };

// Depending on the samplerate, integrating this value can result in an undershoot error on the order of one sample in a matter of seconds (3.84 seconds at 48kHz). Idk if that matters, but there you go.
static inline azaTime azaTimePerSample(uint32_t samplerate) {
	return AZA_CLITERAL(azaTime) { azaTimeOneSecond.time / (int64_t)samplerate };
}



// Base interface to azaQueue entries, must be at the beginning of any derived structs
typedef struct azaQueueEntry {
	azaTime time;
} azaQueueEntry;

typedef struct azaQueue {
	uint16_t entrySizeBytes;
	uint16_t countLimit;
	uint16_t count;
	uint16_t startIndex;
	byte *buffer;
} azaQueue;

// Inits a queue for entries of the given size and an upper bound on how many entries can be queued.
// May return AZA_ERROR_OUT_OF_MEMORY
int azaQueueInit_unchecked(azaQueue *data, uint16_t entrySizeBytes, uint16_t countLimit);
// NOTE: static_asserts that both entrySizeBytes and countLimit are <= UINT16_MAX
#define azaQueueInit(data, entrySizeBytes, countLimit) (\
	static_assert((entrySizeBytes) <= UINT16_MAX, "In " AZA_FUNCTION_NAME " entrySizeBytes (" #entrySizeBytes ") would overflow our uint16_t!");\
	static_assert(((entrySizeBytes) & 0x7) == 0, "In " AZA_FUNCTION_NAME " entrySizeBytes (" #entrySizeBytes ") is not aligned on an 8-byte boundary!");\
	static_assert((countLimit) <= UINT16_MAX, "In " AZA_FUNCTION_NAME " countLimit (" #countLimit ") would overflow our uint16_t!");\
	static_assert((countLimit) > 0, "In " AZA_FUNCTION_NAME " countLimit (" #countLimit ") must be specified!");\
	azaQueueInit_unchecked((data), (entrySizeBytes), (countLimit))\
)
// Frees the buffer
void azaQueueDeinit(azaQueue *data);
// Empties the queue
void azaQueueClear(azaQueue *data);
// Push an entry onto the queue
// returns false if the queue is full
bool azaQueueEnqueue(azaQueue *data, azaQueueEntry *src);
// Pop an entry off the queue and return it
// returns NULL if the queue is empty
azaQueueEntry* azaQueueDequeue(azaQueue *data);
// Peeks at the next entry on the queue
// returns NULL if the queue is empty
azaQueueEntry* azaQueuePeek(azaQueue *data);
// Sorts the queue by time
void azaQueueSort(azaQueue *data);



// Attack Decay Sustain Release envelope config
typedef struct azaADSRConfig {
	// attack time in ms (how long until we hit full volume)
	float attack;
	// decay time in ms (how long after hitting full volume do we hit sustain volume)
	float decay;
	// sustain gain in dB (0.0f is full volume)
	float sustain;
	// release time in ms
	float release;
} azaADSRConfig;

typedef enum azaADSRStage {
	AZA_ADSR_STAGE_STOP,
	AZA_ADSR_STAGE_ATTACK,
	AZA_ADSR_STAGE_DECAY,
	AZA_ADSR_STAGE_SUSTAIN,
	AZA_ADSR_STAGE_RELEASE,
} azaADSRStage;

typedef struct azaADSRInstance {
	azaADSRStage stage;
	// progress along current stage
	float progress;
	// keep track of volume as an early release won't necessarily start at the sustain volume
	float releaseStartAmp;
} azaADSRInstance;

static inline void azaADSRStart(azaADSRInstance *instance) {
	instance->stage = AZA_ADSR_STAGE_ATTACK;
	instance->progress = 0.0f;
	instance->releaseStartAmp = 0.0f;
}

static inline void azaADSRStop(azaADSRInstance *instance) {
	instance->stage = AZA_ADSR_STAGE_RELEASE;
}

float azaADSRGetValue(azaADSRConfig *config, azaADSRInstance *instance);

float azaADSRUpdate(azaADSRConfig *config, azaADSRInstance *instance, float deltaMs);



// Helper to have one value follow a target in a linear fashion (not a decay function)
typedef struct azaFollowerLinear {
	float start;
	float end;
	float progress;
} azaFollowerLinear;

static inline float azaFollowerLinearGetValue(azaFollowerLinear *data) {
	return azaLerpf(data->start, data->end, data->progress);
}

// deltaT represents how far you would progress the follower in a single update from 0.0f to 1.0f
// returns the slope
static inline float azaFollowerLinearGetDerivative(azaFollowerLinear *data, float deltaT) {
	return (data->end - data->start) * deltaT;
}

// Can be called every frame and will automagically handle a moving target
static inline void azaFollowerLinearSetTarget(azaFollowerLinear *data, float target) {
	if (target != data->end) {
		data->start = azaFollowerLinearGetValue(data);
		data->end = target;
		data->progress = 0.0f;
	}
}

// deltaT represents how far to progress the follower in a single update from 0.0f to 1.0f
static inline float azaFollowerLinearUpdate(azaFollowerLinear *data, float deltaT) {
	float result = azaFollowerLinearGetValue(data);
	data->progress = azaMinf(data->progress+deltaT, 1.0f);
	return result;
}

// Can be called every frame and will automagically handle a moving target
// deltaT represents how far to progress the follower in a single update from 0.0f to 1.0f
static inline float azaFollowerLinearUpdateTarget(azaFollowerLinear *data, float target, float deltaT) {
	azaFollowerLinearSetTarget(data, target);
	return azaFollowerLinearUpdate(data, deltaT);
}

// Immediately jumps to the target value with no transition
static inline void azaFollowerLinearJump(azaFollowerLinear *data, float target) {
	data->start = target;
	data->end = target;
	data->progress = 1.0f;
}



typedef struct azaFollowerLinear3D {
	azaVec3 start;
	azaVec3 end;
	float progress;
} azaFollowerLinear3D;

static inline azaVec3 azaFollowerLinear3DGetValue(azaFollowerLinear3D *data) {
	return azaLerpVec3(data->start, data->end, data->progress);
}

// deltaT represents how far you would progress the follower in a single update from 0.0f to 1.0f
// returns the slope
static inline azaVec3 azaFollowerLinear3DGetDerivative(azaFollowerLinear3D *data, float deltaT) {
	return azaMulVec3Scalar(azaSubVec3(data->end, data->start), deltaT);
}

// Can be called every frame and will automagically handle a moving target
static inline void azaFollowerLinear3DSetTarget(azaFollowerLinear3D *data, azaVec3 target) {
	if (!azaVec3Equal(target, data->end)) {
		data->start = azaFollowerLinear3DGetValue(data);
		data->end = target;
		data->progress = 0.0f;
	}
}

// deltaT represents how far to progress the follower in a single update from 0.0f to 1.0f
static inline azaVec3 azaFollowerLinear3DUpdate(azaFollowerLinear3D *data, float deltaT) {
	azaVec3 result = azaFollowerLinear3DGetValue(data);
	data->progress = azaMinf(data->progress+deltaT, 1.0f);
	return result;
}

// Can be called every frame and will automagically handle a moving target
// deltaT represents how far to progress the follower in a single update from 0.0f to 1.0f
static inline azaVec3 azaFollowerLinear3DUpdateTarget(azaFollowerLinear3D *data, azaVec3 target, float deltaT) {
	azaFollowerLinear3DSetTarget(data, target);
	return azaFollowerLinear3DUpdate(data, deltaT);
}

// Immediately jumps to the target value with no transition
static inline void azaFollowerLinear3DJump(azaFollowerLinear3D *data, azaVec3 target) {
	data->start = target;
	data->end = target;
	data->progress = 1.0f;
}



typedef struct azaWorld {
	// Position of our ears
	azaVec3 origin;
	// Must be an orthogonal matrix
	azaMat3 orientation;
	// Speed of sound in units per second.
	// Default: 343.0f (speed of sound in dry air at 20C in m/s)
	float speedOfSound;
} azaWorld;
extern azaWorld azaWorldDefault;
#define AZA_WORLD_DEFAULT ((azaWorld*)0ull)

static inline azaVec3 azaWorldTransformPoint(const azaWorld *world, azaVec3 point) {
	return azaMulVec3Mat3(azaSubVec3(point, world->origin), world->orientation);
}



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_UTILITY_H