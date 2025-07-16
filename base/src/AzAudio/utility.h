/*
	File: utility.h
	Author: Philip Haynes
	Collection of utility functions for implementing plugin interfaces.
*/

#ifndef AZAUDIO_UTILITY_H
#define AZAUDIO_UTILITY_H

#include "math.h"

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

#endif // AZAUDIO_UTILITY_H