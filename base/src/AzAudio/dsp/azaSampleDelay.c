/*
	File: azaSampleDelay.c
	Author: Philip Haynes
*/

#include "azaSampleDelay.h"

#include "../error.h"
#include "../math.h"



void azaSampleDelayInit(azaSampleDelay *data, azaSampleDelayConfig config) {
	data->config = config;
	data->buffer = (azaBuffer) {0};
}

void azaSampleDelayDeinit(azaSampleDelay *data) {
	azaBufferDeinit(&data->buffer, true);
}

static int azaSampleDelayHandleBufferResizes(azaSampleDelay *data, azaChannelLayout layout) {
	return azaBufferResize(&data->buffer, data->config.delayFrames, 0, 0, layout);
}

int azaSampleDelayProcess(azaSampleDelay *data, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	int err = AZA_SUCCESS;
	err = azaCheckBuffersForDSPProcess(dst, src, /* sameFrameCount */ true, /* sameChannelCount */ true);
	if AZA_UNLIKELY(err) return err;

	if (data->config.delayFrames == 0) {
		if (dst->pSamples != src->pSamples) {
			azaBufferCopy(dst, src);
		}
		return AZA_SUCCESS;
	}

	err = azaSampleDelayHandleBufferResizes(data, dst->channelLayout);
	if AZA_UNLIKELY(err) return err;

	uint8_t numSideBuffers = 0;
	azaBuffer sideBuffer;
	if (dst->pSamples == src->pSamples) {
		sideBuffer = azaPushSideBufferCopy(src);
		numSideBuffers++;
		src = &sideBuffer;
	}

	uint32_t carryFrames = AZA_MIN(data->buffer.frames, dst->frames);
	uint32_t preserveFrames = data->buffer.frames - carryFrames;
	uint32_t bodyFrames = dst->frames - carryFrames;

	azaBuffer srcCarry = azaBufferSliceEx(&data->buffer, preserveFrames, carryFrames, 0, 0);
	azaBuffer dstCarry = azaBufferSliceEx(dst, 0, carryFrames, 0, 0);
	azaBufferCopy(&dstCarry, &srcCarry);
	if (preserveFrames) {
		memmove(data->buffer.pSamples + carryFrames * data->buffer.stride, data->buffer.pSamples, sizeof(*data->buffer.pSamples) * preserveFrames * data->buffer.stride);
	}
	srcCarry = azaBufferSliceEx(src, bodyFrames, carryFrames, 0, 0);
	dstCarry = azaBufferSliceEx(&data->buffer, 0, carryFrames, 0, 0);
	azaBufferCopy(&dstCarry, &srcCarry);
	if (bodyFrames) {
		azaBuffer srcBody = azaBufferSliceEx(src, 0, bodyFrames, 0, 0);
		azaBuffer dstBody = azaBufferSliceEx(dst, carryFrames, bodyFrames, 0, 0);
		azaBufferCopy(&dstBody, &srcBody);
	}

	azaPopSideBuffers(numSideBuffers);
	return err;
}


