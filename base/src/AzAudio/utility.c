/*
	File: utility.c
	Author: Philip Haynes
*/

#include "utility.h"

float azaADSRGetValue(azaADSRConfig *config, azaADSRInstance *instance) {
	switch (instance->stage) {
		case AZA_ADSR_STAGE_ATTACK:
			return instance->progress;
		case AZA_ADSR_STAGE_DECAY:
			return azaLerpf(1.0f, config->sustain, instance->progress);
		case AZA_ADSR_STAGE_SUSTAIN:
			return aza_db_to_ampf(config->sustain);
		case AZA_ADSR_STAGE_RELEASE:
			return instance->releaseStartAmp * (1.0f - instance->progress);
		case AZA_ADSR_STAGE_STOP:
			return 0.0f;
	}
	return 0.0f;
}

float azaADSRUpdate(azaADSRConfig *config, azaADSRInstance *instance, float deltaMs) {
	if (instance->stage == AZA_ADSR_STAGE_ATTACK) {
		if (config->attack > 0.0f) {
			float deltaT = deltaMs / config->attack;
			instance->progress += deltaT;
			if (instance->progress >= 1.0f) {
				deltaMs = (instance->progress - 1.0f) * config->attack;
				instance->progress = 0.0f;
				instance->stage = AZA_ADSR_STAGE_DECAY;
			}
		} else {
			instance->stage = AZA_ADSR_STAGE_DECAY;
		}
	}
	if (instance->stage == AZA_ADSR_STAGE_DECAY) {
		if (config->decay > 0.0f) {
			float deltaT = deltaMs / config->decay;
			instance->progress += deltaT;
			if (instance->progress >= 1.0f) {
				instance->stage = AZA_ADSR_STAGE_SUSTAIN;
			}
		} else {
			instance->stage = AZA_ADSR_STAGE_SUSTAIN;
		}
	}
	// We don't do anything during sustain, just wait for release
	if (instance->stage == AZA_ADSR_STAGE_RELEASE) {
		if (config->release > 0.0f) {
			float deltaT = deltaMs / config->release;
			instance->progress += deltaT;
			if (instance->progress >= 1.0f) {
				instance->stage = AZA_ADSR_STAGE_STOP;
			}
		} else {
			instance->stage = AZA_ADSR_STAGE_STOP;
		}
	}
	float result = azaADSRGetValue(config, instance);
	if (instance->stage != AZA_ADSR_STAGE_RELEASE) {
		instance->releaseStartAmp = result;
	}
	return result;
}