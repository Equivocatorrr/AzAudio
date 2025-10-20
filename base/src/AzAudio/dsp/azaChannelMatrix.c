/*
	File: azaChannelMatrix.c
	Author: Philip Haynes
*/

#include "azaChannelMatrix.h"

#include "../AzAudio.h"
#include "../error.h"

#include <stdlib.h>

#ifdef _MSC_VER
#define AZAUDIO_NO_THREADS_H
#define thread_local __declspec( thread )
#define alloca _alloca
#else
#include <alloca.h>
#endif

int azaChannelMatrixInit(azaChannelMatrix *data, uint8_t inputs, uint8_t outputs) {
	data->inputs = inputs;
	data->outputs = outputs;
	uint16_t total = (uint16_t)inputs * (uint16_t)outputs;
	if (total > 0) {
		data->matrix = (float*)aza_calloc(total, sizeof(float));
		if (!data->matrix) {
			return AZA_ERROR_OUT_OF_MEMORY;
		}
	} else {
		data->matrix = NULL;
	}
	return AZA_SUCCESS;
}

void azaChannelMatrixDeinit(azaChannelMatrix *data) {
	if (data->matrix) {
		aza_free(data->matrix);
	}
}

struct DistChannelPair {
	int16_t dist, dstC;
};

static int compareDistChannelPair(const void *_lhs, const void *_rhs) {
	struct DistChannelPair lhs = *((struct DistChannelPair*)_lhs);
	struct DistChannelPair rhs = *((struct DistChannelPair*)_rhs);
	return lhs.dist - rhs.dist;
}

void azaChannelMatrixGenerateRoutingFromLayouts(azaChannelMatrix *data, azaChannelLayout srcLayout, azaChannelLayout dstLayout) {
	assert(data->inputs == srcLayout.count);
	assert(data->outputs == dstLayout.count);
	assert(srcLayout.count > 0);
	assert(dstLayout.count > 0);
	if (dstLayout.count == 1) {
		// Just make them all connect to the one singular output channel
		for (int16_t srcC = 0; srcC < srcLayout.count; srcC++) {
			data->matrix[data->outputs * srcC] = 1.0f;
		}
		return;
	}
	bool srcChannelUsed[256];
	int16_t srcChannelsUsed = 0;
	for (int16_t srcC = 0; srcC < srcLayout.count; srcC++) {
		srcChannelUsed[srcC] = false;
		for (int16_t dstC = 0; dstC < dstLayout.count; dstC++) {
			if (srcLayout.positions[srcC] == dstLayout.positions[dstC]) {
				srcChannelUsed[srcC] = true;
				data->matrix[data->outputs * srcC + dstC] = 1.0f;
				srcChannelsUsed++;
				break;
			}
		}
	}
	if (srcChannelsUsed < srcLayout.count) {
		// Try and find 2 closest channels for each channel not already mapped
		for (int16_t srcC = 0; srcC < srcLayout.count; srcC++) {
			if (srcChannelUsed[srcC]) continue;
			struct DistChannelPair *list = alloca(dstLayout.count * sizeof(struct DistChannelPair));
			int16_t bdi = 0;
			for (int16_t dstC = 0; dstC < dstLayout.count; dstC++) {
				uint16_t dist = azaPositionDistance(srcLayout.positions[srcC], dstLayout.positions[dstC]);
				list[bdi++] = (struct DistChannelPair){ dist, dstC };
			}
			qsort(list, dstLayout.count, sizeof(*list), compareDistChannelPair);
			// Pick the first 2 in the list as they should be the 2 closest
			// Set weights based on respective distances
			float totalDist = (float)(list[0].dist + list[1].dist);
			data->matrix[data->outputs * srcC + list[0].dstC] = 1.0f - ((float)list[0].dist / totalDist);
			data->matrix[data->outputs * srcC + list[1].dstC] = 1.0f - ((float)list[1].dist / totalDist);
		}
	}
}
