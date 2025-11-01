/*
	File: azaBuffer.c
	Author: Philip Haynes
*/

#include "azaBuffer.h"

#include "../error.h"
#include "../AzAudio.h"
#include "../math.h"
#include <threads.h> // thread_local



int azaCheckBuffer_full(const char *context, azaBuffer *buffer) {
	if (!context) {
		context = "azaCheckBuffer";
	}
	if (buffer == NULL) {
		AZA_LOG_ERR("Error(%s): The buffer itself is NULL\n", context);
		return AZA_ERROR_NULL_POINTER;
	}
	if (buffer->pSamples == NULL) {
		AZA_LOG_ERR("Error(%s): pSamples is NULL\n", context);
		return AZA_ERROR_NULL_POINTER;
	}
	if (buffer->channelLayout.count == 0) {
		AZA_LOG_ERR("Error(%s): channelLayout.count is 0\n", context);
		return AZA_ERROR_INVALID_CHANNEL_COUNT;
	}
	if (buffer->channelLayout.count > AZA_MAX_CHANNEL_POSITIONS) {
		AZA_LOG_ERR("Error(%s): channelLayout.count is %u, greater than our maximum of %u\n", context, (uint32_t)buffer->channelLayout.count, AZA_MAX_CHANNEL_POSITIONS);
		return AZA_ERROR_INVALID_CHANNEL_COUNT;
	}
	uint32_t totalFrames = azaBufferGetTotalFrameCount(buffer);
	if (totalFrames == 0) {
		AZA_LOG_ERR("Error(%s): total frame count is 0\n", context);
		return AZA_ERROR_INVALID_FRAME_COUNT;
	}
	if (totalFrames > (UINT32_MAX / buffer->channelLayout.count)) {
		AZA_LOG_ERR("Error(%s): total frame count is %u, which would overflow with %u channels (max total frame count is %u)\n", context, totalFrames, (uint32_t)buffer->channelLayout.count, UINT32_MAX / buffer->channelLayout.count);
		return AZA_ERROR_INVALID_FRAME_COUNT;
	}
	return AZA_SUCCESS;
}

int azaCheckBuffersForDSPProcess_full(const char *context, azaBuffer *dst, azaBuffer *src, bool sameFrameCount, bool sameChannelCount) {
	if (!context) {
		context = "azaCheckBuffersForDSPProcess";
	}
	int err = AZA_SUCCESS;
	err = azaCheckBuffer_full(context, dst);
	if AZA_UNLIKELY(err) return err;
	err = azaCheckBuffer_full(context, src);
	if AZA_UNLIKELY(err) return err;
	if AZA_UNLIKELY(sameFrameCount && dst->frames != src->frames) {
		AZA_LOG_ERR("Error(%s): dst and src frame counts do not match! dst has %u frames and src has %u frames.\n", context, dst->frames, src->frames);
		return AZA_ERROR_MISMATCHED_FRAME_COUNT;
	}
	if AZA_UNLIKELY(sameChannelCount && dst->channelLayout.count != src->channelLayout.count) {
		AZA_LOG_ERR("Error(%s): dst and src channel counts do not match! dst has %u channels and src has %u channels.\n", context, (uint32_t)dst->channelLayout.count, (uint32_t)src->channelLayout.count);
		return AZA_ERROR_MISMATCHED_CHANNEL_COUNT;
	}
	return AZA_SUCCESS;
}

int azaBufferInit(azaBuffer *buffer, uint32_t frames, uint32_t leadingFrames, uint32_t trailingFrames, azaChannelLayout channelLayout) {
	uint32_t totalFrames = frames + leadingFrames + trailingFrames;
	assert(totalFrames > 0);
	assert(channelLayout.count > 0);
	buffer->bufferCapacity = totalFrames * channelLayout.count;
	buffer->buffer = (float*)aza_calloc(buffer->bufferCapacity, sizeof(float));
	if (!buffer->buffer) return AZA_ERROR_OUT_OF_MEMORY;
	buffer->pSamples = buffer->buffer + leadingFrames * channelLayout.count;
	buffer->frames = frames;
	buffer->leadingFrames = leadingFrames;
	buffer->trailingFrames = trailingFrames;
	buffer->stride = channelLayout.count;
	buffer->channelLayout = channelLayout;
	return AZA_SUCCESS;
}

void azaBufferDeinit(azaBuffer *buffer, bool warnOnUnowned) {
	if (!buffer->buffer) {
		if (warnOnUnowned) {
			AZA_LOG_ERR("Warning: Called azaBufferDeinit on an unowned buffer\n");
		}
	} else {
		aza_free(buffer->buffer);
		buffer->buffer = NULL;
		buffer->bufferCapacity = 0;
		buffer->pSamples = NULL;
	}
}

int azaBufferResize(azaBuffer *buffer, uint32_t frames, uint32_t leadingFrames, uint32_t trailingFrames, azaChannelLayout channelLayout) {
	if (buffer->pSamples) {
		assert(buffer->buffer && "azaBufferResize is only for owned buffers");
	}
	uint32_t totalFrames = frames + leadingFrames + trailingFrames;
	uint32_t neededCapacity = totalFrames * channelLayout.count;
	if (neededCapacity > buffer->bufferCapacity) {
		neededCapacity = (uint32_t)aza_grow(buffer->bufferCapacity, neededCapacity, 16);
		if (leadingFrames == buffer->leadingFrames) {
			float *newBuffer = aza_realloc(buffer->buffer, sizeof(*buffer->buffer) * neededCapacity);
			if (!newBuffer) {
				return AZA_ERROR_OUT_OF_MEMORY;
			}
			buffer->buffer = newBuffer;
			memset(buffer->buffer + buffer->bufferCapacity, 0, sizeof(*buffer->buffer) * (neededCapacity - buffer->bufferCapacity));
		} else {
			float *newBuffer = aza_calloc(neededCapacity, sizeof(*buffer->buffer));
			if (!newBuffer) {
				return AZA_ERROR_OUT_OF_MEMORY;
			}
			if (buffer->buffer) {
				if (channelLayout.count == buffer->channelLayout.count) {
					memcpy(newBuffer + (leadingFrames - buffer->leadingFrames) * channelLayout.count, buffer->buffer, sizeof(*buffer->buffer) * buffer->bufferCapacity);
				}
				aza_free(buffer->buffer);
			}
			buffer->buffer = newBuffer;
		}
		buffer->bufferCapacity = neededCapacity;
	} else if (leadingFrames != buffer->leadingFrames && channelLayout.count == buffer->channelLayout.count) {
		uint32_t bufferTotalFrames = azaBufferGetTotalFrameCount(buffer);
		if (leadingFrames > buffer->leadingFrames) {
			// Move the data into the right place sir
			memmove(buffer->buffer + (leadingFrames - buffer->leadingFrames) * channelLayout.count, buffer->buffer, sizeof(*buffer->buffer) * bufferTotalFrames * channelLayout.count);
			// Zero the start
			memset(buffer->buffer, 0, sizeof(*buffer->buffer) * (leadingFrames - buffer->leadingFrames) * channelLayout.count);
		} else {
			// Move the data into the left place sir
			memmove(buffer->buffer, buffer->buffer + (buffer->leadingFrames - leadingFrames) * channelLayout.count, sizeof(*buffer->buffer) * bufferTotalFrames * channelLayout.count);
		}
		if (frames + trailingFrames > buffer->frames + buffer->trailingFrames) {
			// Zero the end
			memset(buffer->buffer + bufferTotalFrames * channelLayout.count, 0, sizeof(*buffer->buffer) * (totalFrames - bufferTotalFrames) * channelLayout.count);
		}
	}
	buffer->pSamples = buffer->buffer + leadingFrames * channelLayout.count;
	buffer->frames = frames;
	buffer->leadingFrames = leadingFrames;
	buffer->trailingFrames = trailingFrames;
	buffer->stride = channelLayout.count;
	buffer->channelLayout = channelLayout;
	if (channelLayout.count != buffer->channelLayout.count) {
		azaBufferZero(buffer);
	}
	return AZA_SUCCESS;
}

void azaBufferZero(azaBuffer *buffer) {
	uint32_t totalFrames = azaBufferGetTotalFrameCount(buffer);
	if (buffer->pSamples && totalFrames && buffer->channelLayout.count) {
		if AZA_LIKELY(buffer->channelLayout.count == buffer->stride) {
			memset(buffer->pSamples - buffer->leadingFrames * buffer->stride, 0, sizeof(float) * totalFrames * buffer->channelLayout.count);
		} else {
			for (int32_t i = -(int32_t)buffer->leadingFrames * buffer->stride; i < (int32_t)(buffer->frames + buffer->trailingFrames) * buffer->stride; i += buffer->stride) {
				for (uint8_t c = 0; c < buffer->channelLayout.count; c++) {
					buffer->pSamples[i + c] = 0.0f;
				}
			}
		}
	}
}

// azaBufferDeinterlace implementation is in specialized/azaBufferDeinterlace.c

// azaBufferReinterlace implementation is in specialized/azaBufferReinterlace.c

void azaBufferMix(azaBuffer *dst, float volumeDst, azaBuffer *src, float volumeSrc) {
	assert(dst->frames == src->frames);
	assert(dst->channelLayout.count == src->channelLayout.count);
	const uint32_t channels = dst->channelLayout.count;
	if AZA_UNLIKELY(volumeDst == 1.0f && volumeSrc == 0.0f) {
		return;
	} else if AZA_UNLIKELY(volumeDst == 0.0f && volumeSrc == 0.0f) {
		azaBufferZero(dst);
	} else if (volumeDst == 1.0f && volumeSrc == 1.0f) {
		for (uint32_t i = 0; i < dst->frames; i++) {
			for (uint32_t c = 0; c < channels; c++) {
				dst->pSamples[i * dst->stride + c] = dst->pSamples[i * dst->stride + c] + src->pSamples[i * src->stride + c];
			}
		}
	} else if AZA_LIKELY(volumeDst == 1.0f) {
		for (uint32_t i = 0; i < dst->frames; i++) {
			for (uint32_t c = 0; c < channels; c++) {
				dst->pSamples[i * dst->stride + c] = dst->pSamples[i * dst->stride + c] + src->pSamples[i * src->stride + c] * volumeSrc;
			}
		}
	} else if (volumeSrc == 1.0f) {
		for (uint32_t i = 0; i < dst->frames; i++) {
			for (uint32_t c = 0; c < channels; c++) {
				dst->pSamples[i * dst->stride + c] = dst->pSamples[i * dst->stride + c] * volumeDst + src->pSamples[i * src->stride + c];
			}
		}
	} else {
		for (uint32_t i = 0; i < dst->frames; i++) {
			for (uint32_t c = 0; c < channels; c++) {
				dst->pSamples[i * dst->stride + c] = dst->pSamples[i * dst->stride + c] * volumeDst + src->pSamples[i * src->stride + c] * volumeSrc;
			}
		}
	}
}

void azaBufferMixFadeEase(azaBuffer *dst, float volumeDstStart, float volumeDstEnd, fp_azaEase_t easeDst, azaBuffer *src, float volumeSrcStart, float volumeSrcEnd, fp_azaEase_t easeSrc) {
	if (volumeDstStart == volumeDstEnd && volumeSrcStart == volumeSrcEnd) {
		azaBufferMix(dst, volumeDstStart, src, volumeSrcStart);
		return;
	}
	if ((easeDst == azaEaseLinear || volumeDstStart == volumeDstEnd) && (easeSrc == azaEaseLinear || volumeSrcStart == volumeSrcEnd)) {
		azaBufferMixFadeLinear(dst, volumeDstStart, volumeDstEnd, src, volumeSrcStart, volumeSrcEnd);
		return;
	}
	assert(dst->frames == src->frames);
	assert(dst->channelLayout.count == src->channelLayout.count);
	assert(easeDst);
	assert(easeSrc);
	const uint32_t channels = dst->channelLayout.count;
	const float volumeDstDelta = volumeDstEnd - volumeDstStart;
	const float volumeSrcDelta = volumeSrcEnd - volumeSrcStart;
	const float framesF = (float)dst->frames;
	if (volumeDstDelta == 0.0f) {
		// No fading necessary for dst
		if (volumeDstStart == 1.0f) {
			// No attenuation necessary for dst
			for (uint32_t i = 0; i < dst->frames; i++) {
				float t = (float)i / framesF;
				float volumeSrc = volumeSrcStart + volumeSrcDelta * easeSrc(t);
				for (uint32_t c = 0; c < channels; c++) {
					dst->pSamples[i * dst->stride + c] = dst->pSamples[i * dst->stride + c] + src->pSamples[i * src->stride + c] * volumeSrc;
				}
			}
		} else {
			// We must still attenuate dst, just without a fade
			for (uint32_t i = 0; i < dst->frames; i++) {
				float t = (float)i / framesF;
				float volumeSrc = volumeSrcStart + volumeSrcDelta * easeSrc(t);
				for (uint32_t c = 0; c < channels; c++) {
					dst->pSamples[i * dst->stride + c] = dst->pSamples[i * dst->stride + c] + src->pSamples[i * src->stride + c] * volumeSrc;
				}
			}
		}
	} else {
		// Fading is necessary for dst
		for (uint32_t i = 0; i < dst->frames; i++) {
			float t = (float)i / framesF;
			float volumeDst = volumeDstStart + volumeDstDelta * easeDst(t);
			float volumeSrc = volumeSrcStart + volumeSrcDelta * easeSrc(t);
			for (uint32_t c = 0; c < channels; c++) {
				dst->pSamples[i * dst->stride + c] = dst->pSamples[i * dst->stride + c] * volumeDst + src->pSamples[i * src->stride + c] * volumeSrc;
			}
		}
	}
}

void azaBufferMixFadeLinear(azaBuffer *dst, float volumeDstStart, float volumeDstEnd, azaBuffer *src, float volumeSrcStart, float volumeSrcEnd) {
	if (volumeDstStart == volumeDstEnd && volumeSrcStart == volumeSrcEnd) {
		azaBufferMix(dst, volumeDstStart, src, volumeSrcStart);
		return;
	}
	assert(dst->frames == src->frames);
	assert(dst->channelLayout.count == src->channelLayout.count);
	const uint32_t channels = dst->channelLayout.count;
	const float volumeDstDelta = volumeDstEnd - volumeDstStart;
	const float volumeSrcDelta = volumeSrcEnd - volumeSrcStart;
	const float framesF = (float)dst->frames;
	if (volumeDstDelta == 0.0f) {
		// No fading necessary for dst
		if (volumeDstStart == 1.0f) {
			// No attenuation necessary for dst
			for (uint32_t i = 0; i < dst->frames; i++) {
				float t = (float)i / framesF;
				float volumeSrc = volumeSrcStart + volumeSrcDelta * t;
				for (uint32_t c = 0; c < channels; c++) {
					dst->pSamples[i * dst->stride + c] = dst->pSamples[i * dst->stride + c] + src->pSamples[i * src->stride + c] * volumeSrc;
				}
			}
		} else {
			// We must still attenuate dst, just without a fade
			for (uint32_t i = 0; i < dst->frames; i++) {
				float t = (float)i / framesF;
				float volumeSrc = volumeSrcStart + volumeSrcDelta * t;
				for (uint32_t c = 0; c < channels; c++) {
					dst->pSamples[i * dst->stride + c] = dst->pSamples[i * dst->stride + c] + src->pSamples[i * src->stride + c] * volumeSrc;
				}
			}
		}
	} else {
		// Fading is necessary for dst
		for (uint32_t i = 0; i < dst->frames; i++) {
			float t = (float)i / framesF;
			float volumeDst = volumeDstStart + volumeDstDelta * t;
			float volumeSrc = volumeSrcStart + volumeSrcDelta * t;
			for (uint32_t c = 0; c < channels; c++) {
				dst->pSamples[i * dst->stride + c] = dst->pSamples[i * dst->stride + c] * volumeDst + src->pSamples[i * src->stride + c] * volumeSrc;
			}
		}
	}
}

// azaBufferMixMatrix implementation is in specialized/azaBufferMixMatrix.c

void azaBufferCopy(azaBuffer *dst, azaBuffer *src) {
	assert(dst->frames == src->frames);
	assert(dst->channelLayout.count == src->channelLayout.count);
	uint32_t leadingFrames = AZA_MIN(dst->leadingFrames, src->leadingFrames);
	uint32_t trailingFrames = AZA_MIN(dst->trailingFrames, src->trailingFrames);
	uint32_t totalFrames = src->frames + leadingFrames + trailingFrames;
	if (dst->channelLayout.count == dst->stride && src->channelLayout.count == src->stride) {
		uint32_t leadingSamples = leadingFrames * src->channelLayout.count;
		memcpy(dst->pSamples - leadingSamples, src->pSamples - leadingSamples, sizeof(float) * totalFrames * src->channelLayout.count);
	} else {
		for (int64_t i = -(int64_t)leadingFrames; i < (int64_t)(src->frames + trailingFrames); i++) {
			for (uint8_t c = 0; c < src->channelLayout.count; c++) {
				dst->pSamples[i * dst->stride + c] = src->pSamples[i * src->stride + c];
			}
		}
	}
}

void azaBufferCopyChannel(azaBuffer *dst, uint8_t channelDst, azaBuffer *src, uint8_t channelSrc) {
	assert(dst->frames == src->frames);
	assert(channelDst < dst->channelLayout.count);
	assert(channelSrc < src->channelLayout.count);
	uint32_t leadingFrames = AZA_MIN(dst->leadingFrames, src->leadingFrames);
	uint32_t trailingFrames = AZA_MIN(dst->trailingFrames, src->trailingFrames);
	uint32_t totalFrames = src->frames + leadingFrames + trailingFrames;
	if (dst->stride == 1 && src->stride == 1) {
		uint32_t leadingSamples = leadingFrames;
		memcpy(dst->pSamples - leadingSamples, src->pSamples - leadingSamples, sizeof(float) * totalFrames);
	} else if (dst->stride == 1) {
		for (int64_t i = 0; i < (int64_t)(dst->frames + trailingFrames); i++) {
			dst->pSamples[i] = src->pSamples[i * src->stride + channelSrc];
		}
	} else if (src->stride == 1) {
		for (int64_t i = 0; i < (int64_t)(dst->frames + trailingFrames); i++) {
			dst->pSamples[i * dst->stride + channelDst] = src->pSamples[i];
		}
	} else {
		for (int64_t i = 0; i < (int64_t)(dst->frames + trailingFrames); i++) {
			dst->pSamples[i * dst->stride + channelDst] = src->pSamples[i * src->stride + channelSrc];
		}
	}
}

void azaBufferBroadcastChannel(azaBuffer *dst, azaBuffer *src, uint8_t channelSrc) {
	assert(dst->frames == src->frames);
	assert(channelSrc < src->channelLayout.count);
	uint32_t leadingFrames = AZA_MIN(dst->leadingFrames, src->leadingFrames);
	uint32_t trailingFrames = AZA_MIN(dst->trailingFrames, src->trailingFrames);
	uint32_t totalFrames = src->frames + leadingFrames + trailingFrames;
	if (dst->stride == 1 && src->stride == 1) {
		uint32_t leadingSamples = leadingFrames;
		memcpy(dst->pSamples - leadingSamples, src->pSamples - leadingSamples, sizeof(float) * totalFrames);
	} else if (dst->stride == 1) {
		for (int64_t i = 0; i < (int64_t)(dst->frames + trailingFrames); i++) {
			dst->pSamples[i] = src->pSamples[i * src->stride + channelSrc];
		}
	} else {
		for (int64_t i = 0; i < (int64_t)(dst->frames + trailingFrames); i++) {
			float sample = src->pSamples[i * src->stride + channelSrc];
			for (uint8_t c = 0; c < dst->channelLayout.count; c++) {
				dst->pSamples[i * dst->stride + c] = sample;
			}
		}
	}
}

azaBuffer azaBufferView(azaBuffer *src) {
	azaBuffer result = *src;
	result.buffer = NULL;
	result.bufferCapacity = 0;
	return result;
}

azaBuffer azaBufferSlice(azaBuffer *src, uint32_t frameStart, uint32_t frameCount) {
	assert(frameStart < src->frames);
	assert(frameCount <= src->frames - frameStart);
	uint32_t srcEndFrame = src->frames + src->trailingFrames;
	return (azaBuffer) {
		.pSamples       = src->pSamples + frameStart * src->stride,
		.samplerate     = src->samplerate,
		.frames         = frameCount,
		.leadingFrames  = src->leadingFrames + frameStart,
		.trailingFrames = srcEndFrame - (frameStart + frameCount),
		.stride         = src->stride,
		.channelLayout  = src->channelLayout,
	};
}

azaBuffer azaBufferSliceEx(azaBuffer *src, uint32_t frameStart, uint32_t frameCount, uint32_t leadingFrames, uint32_t trailingFrames) {
	assert(leadingFrames <= src->leadingFrames);
	assert(frameStart < src->frames);
	assert(frameCount+trailingFrames <= src->frames + src->trailingFrames - frameStart);
	return (azaBuffer) {
		.pSamples       = src->pSamples + frameStart * src->stride,
		.samplerate     = src->samplerate,
		.frames         = frameCount,
		.leadingFrames  = leadingFrames,
		.trailingFrames = trailingFrames,
		.stride         = src->stride,
		.channelLayout  = src->channelLayout,
	};
}

azaBuffer azaBufferOneChannel(azaBuffer *src, uint8_t channel) {
	return (azaBuffer) {
		.pSamples       = src->pSamples + channel,
		.samplerate     = src->samplerate,
		.frames         = src->frames,
		.leadingFrames  = src->leadingFrames,
		.trailingFrames = src->trailingFrames,
		.stride         = src->stride,
		.channelLayout  = azaChannelLayoutOneChannel(src->channelLayout, channel),
	};
}

azaBuffer azaBufferOneSample(float *sample, uint32_t samplerate) {
	return (azaBuffer) {
		.pSamples       = sample,
		.samplerate     = samplerate,
		.frames         = 1,
		.leadingFrames  = 0,
		.trailingFrames = 0,
		.stride         = 1,
		.channelLayout  = azaChannelLayoutMono(),
	};
}



// Side Buffers



// TODO: Maybe make this dynamic, and also deal with the thread_local memory leak snafu (possibly by making it not a stack, and therefore no longer thread_local)
#define AZA_MAX_SIDE_BUFFERS 64
thread_local azaBuffer sideBufferPool[AZA_MAX_SIDE_BUFFERS] = {{0}};
thread_local size_t sideBuffersInUse = 0;

azaBuffer azaPushSideBuffer(uint32_t frames, uint32_t leadingFrames, uint32_t trailingFrames, uint32_t channels, uint32_t samplerate) {
	assert(sideBuffersInUse < AZA_MAX_SIDE_BUFFERS);
	azaBuffer *buffer = &sideBufferPool[sideBuffersInUse];
	azaBufferResize(buffer, frames, leadingFrames, trailingFrames, (azaChannelLayout) { .count = channels });
	buffer->samplerate = samplerate;
	sideBuffersInUse++;
	return *buffer;
}

azaBuffer azaPushSideBufferZero(uint32_t frames, uint32_t leadingFrames, uint32_t trailingFrames, uint32_t channels, uint32_t samplerate) {
	azaBuffer buffer = azaPushSideBuffer(frames, leadingFrames, trailingFrames, channels, samplerate);
	azaBufferZero(&buffer);
	return buffer;
}

azaBuffer azaPushSideBufferCopy(azaBuffer *src) {
	azaBuffer result = azaPushSideBuffer(src->frames, src->leadingFrames, src->trailingFrames, src->channelLayout.count, src->samplerate);
	azaBufferCopy(&result, src);
	return result;
}

azaBuffer azaPushSideBufferCopyZero(azaBuffer *src) {
	azaBuffer result = azaPushSideBufferZero(src->frames, src->leadingFrames, src->trailingFrames, src->channelLayout.count, src->samplerate);
	return result;
}

void azaPopSideBuffer() {
	assert(sideBuffersInUse >= 1);
	sideBuffersInUse--;
}

void azaPopSideBuffers(uint8_t count) {
	assert(sideBuffersInUse >= count);
	sideBuffersInUse -= count;
}