/*
	File: utility.c
	Author: Philip Haynes
*/

#include "utility.h"
#include "../AzAudio.h"
#include "../error.h"

#include <stdlib.h>

azaWorld azaWorldDefault;



int azaQueueInit_unchecked(azaQueue *data, uint16_t entrySizeBytes, uint16_t countLimit) {
	data->entrySizeBytes = entrySizeBytes;
	data->countLimit = countLimit;
	data->count = 0;
	data->startIndex = 0;
	// Double countLimit so we can just ignore wraparound. Wastes memory, but let's see if we actually care.
	data->buffer = aza_calloc((size_t)countLimit * 2, (size_t)entrySizeBytes);
	if (!data->buffer) {
		return AZA_ERROR_OUT_OF_MEMORY;
	}
	return AZA_SUCCESS;
}

void azaQueueDeinit(azaQueue *data) {
	if (data->buffer) {
		aza_free(data->buffer);
	}
}

void azaQueueClear(azaQueue *data) {
	data->count = 0;
	data->startIndex = 0;
}

bool azaQueueEnqueue(azaQueue *data, azaQueueEntry *src) {
	if (data->count >= data->countLimit) {
		return false;
	}
	// Prefer to do this in Enqueue rather than Dequeue because Dequeue will be happening in the sound thread, while Enqueue may be happening in a less performance-critical place.
	if (data->startIndex >= data->countLimit) {
		memmove(data->buffer, data->buffer + (size_t)data->startIndex * (size_t)data->entrySizeBytes, (size_t)data->count * (size_t)data->entrySizeBytes);
		data->startIndex = 0;
	}
	size_t dstIndex = (size_t)data->startIndex + (size_t)data->count;
	azaQueueEntry *dst = (azaQueueEntry*)(data->buffer + dstIndex * (size_t)data->entrySizeBytes);
	memcpy(dst, src, data->entrySizeBytes);
	return true;
}

azaQueueEntry* azaQueueDequeue(azaQueue *data) {
	azaQueueEntry *result;
	if (data->count) {
		result = (azaQueueEntry*)(data->buffer + (size_t)data->startIndex * (size_t)data->entrySizeBytes);
		data->startIndex++;
		data->count--;
	} else {
		result = NULL;
	}
	return result;
}

azaQueueEntry* azaQueuePeek(azaQueue *data) {
	azaQueueEntry *result;
	if (data->count) {
		result = (azaQueueEntry*)(data->buffer + (size_t)data->startIndex * (size_t)data->entrySizeBytes);
	} else {
		result = NULL;
	}
	return result;
}

static int compareQueueEntryTime(const void *_lhs, const void *_rhs) {
	const azaQueueEntry *lhs = _lhs;
	const azaQueueEntry *rhs = _rhs;
	// Sort descending
	if (lhs->time.time < rhs->time.time) return 1;
	if (lhs->time.time > rhs->time.time) return -1;
	return 0;
}

void azaQueueSort(azaQueue *data) {
	if (data->count >= 2) {
		void *bufferStart = data->buffer + (size_t)data->startIndex * (size_t)data->entrySizeBytes;
		qsort(bufferStart, data->count, data->entrySizeBytes, compareQueueEntryTime);
	}
}



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