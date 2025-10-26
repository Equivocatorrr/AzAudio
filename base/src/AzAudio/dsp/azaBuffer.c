/*
	File: azaBuffer.c
	Author: Philip Haynes
*/

#include "azaBuffer.h"

#include "../error.h"
#include "../AzAudio.h"
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

int azaBufferInit(azaBuffer *buffer, uint32_t frames, uint32_t framesLeading, uint32_t framesTrailing, azaChannelLayout channelLayout) {
	uint32_t totalFrames = frames + framesLeading + framesTrailing;
	assert(totalFrames > 0);
	assert(channelLayout.count > 0);
	buffer->pSamples = (float*)aza_calloc(totalFrames * channelLayout.count, sizeof(float));
	if (!buffer->pSamples) return AZA_ERROR_OUT_OF_MEMORY;
	buffer->pSamples += framesLeading * channelLayout.count;
	buffer->frames = frames;
	buffer->framesLeading = framesLeading;
	buffer->framesTrailing = framesTrailing;
	buffer->stride = channelLayout.count;
	buffer->owned = true;
	buffer->channelLayout = channelLayout;
	return AZA_SUCCESS;
}

void azaBufferDeinit(azaBuffer *buffer, bool warnOnUnowned) {
	if (!buffer->owned) {
		if (warnOnUnowned) {
			AZA_LOG_ERR("Warning: Called azaBufferDeinit on an unowned buffer\n");
		}
	} else {
		aza_free(azaBufferGetStart(buffer));
	}
}

void azaBufferZero(azaBuffer *buffer) {
	uint32_t totalFrames = azaBufferGetTotalFrameCount(buffer);
	if (buffer->pSamples && totalFrames && buffer->channelLayout.count) {
		float *pSamplesStart = azaBufferGetStart(buffer);
		if AZA_LIKELY(buffer->channelLayout.count == buffer->stride) {
			memset(pSamplesStart, 0, sizeof(float) * totalFrames * buffer->channelLayout.count);
		} else {
			for (uint32_t i = 0; i < totalFrames * buffer->stride; i += buffer->stride) {
				for (uint8_t c = 0; c < buffer->channelLayout.count; c++) {
					pSamplesStart[i + c] = 0.0f;
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
	if (dst->channelLayout.count == dst->stride && src->channelLayout.count == src->stride) {
		memcpy(dst->pSamples, src->pSamples, sizeof(float) * src->frames * src->channelLayout.count);
	} else {
		for (uint32_t i = 0; i < src->frames; i++) {
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
	if (dst->stride == 1 && src->stride == 1) {
		memcpy(dst->pSamples, src->pSamples, sizeof(float) * dst->frames);
	} else if (dst->stride == 1) {
		for (uint32_t i = 0; i < dst->frames; i++) {
			dst->pSamples[i] = src->pSamples[i * src->stride + channelSrc];
		}
	} else if (src->stride == 1) {
		for (uint32_t i = 0; i < dst->frames; i++) {
			dst->pSamples[i * dst->stride + channelDst] = src->pSamples[i];
		}
	} else {
		for (uint32_t i = 0; i < dst->frames; i++) {
			dst->pSamples[i * dst->stride + channelDst] = src->pSamples[i * src->stride + channelSrc];
		}
	}
}

void azaBufferBroadcastChannel(azaBuffer *dst, azaBuffer *src, uint8_t channelSrc) {
	assert(dst->frames == src->frames);
	assert(channelSrc < src->channelLayout.count);
	if (dst->stride == 1 && src->stride == 1) {
		memcpy(dst->pSamples, src->pSamples, sizeof(float) * dst->frames);
	} else if (dst->stride == 1) {
		for (uint32_t i = 0; i < dst->frames; i++) {
			dst->pSamples[i] = src->pSamples[i * src->stride + channelSrc];
		}
	} else {
		for (uint32_t i = 0; i < dst->frames; i++) {
			float sample = src->pSamples[i * src->stride + channelSrc];
			for (uint8_t c = 0; c < dst->channelLayout.count; c++) {
				dst->pSamples[i * dst->stride + c] = sample;
			}
		}
	}
}

azaBuffer azaBufferSlice(azaBuffer *src, uint32_t frameStart, uint32_t frameCount) {
	assert(frameStart < src->frames);
	assert(frameCount <= src->frames - frameStart);
	uint32_t srcEndFrame = src->frames + src->framesTrailing;
	return (azaBuffer) {
		.pSamples       = src->pSamples + frameStart * src->stride,
		.samplerate     = src->samplerate,
		.frames         = frameCount,
		.framesLeading  = src->framesLeading + frameStart,
		.framesTrailing = srcEndFrame - (frameStart + frameCount),
		.stride         = src->stride,
		.owned          = false,
		.channelLayout  = src->channelLayout,
	};
}

azaBuffer azaBufferOneChannel(azaBuffer *src, uint8_t channel) {
	return (azaBuffer) {
		.pSamples       = src->pSamples + channel,
		.samplerate     = src->samplerate,
		.frames         = src->frames,
		.framesLeading  = src->framesLeading,
		.framesTrailing = src->framesTrailing,
		.stride         = src->stride,
		.owned          = false,
		.channelLayout  = azaChannelLayoutOneChannel(src->channelLayout, channel),
	};
}

azaBuffer azaBufferOneSample(float *sample, uint32_t samplerate) {
	return (azaBuffer) {
		.pSamples       = sample,
		.samplerate     = samplerate,
		.frames         = 1,
		.framesLeading  = 0,
		.framesTrailing = 0,
		.stride         = 1,
		.owned          = false,
		.channelLayout  = azaChannelLayoutMono(),
	};
}



// Side Buffers



// TODO: Maybe make this dynamic, and also deal with the thread_local memory leak snafu (possibly by making it not a stack, and therefore no longer thread_local)
#define AZA_MAX_SIDE_BUFFERS 64
thread_local azaBuffer sideBufferPool[AZA_MAX_SIDE_BUFFERS] = {{0}};
thread_local size_t sideBufferCapacity[AZA_MAX_SIDE_BUFFERS] = {0};
thread_local size_t sideBuffersInUse = 0;

azaBuffer azaPushSideBuffer(uint32_t frames, uint32_t framesLeading, uint32_t framesTrailing, uint32_t channels, uint32_t samplerate) {
	assert(sideBuffersInUse < AZA_MAX_SIDE_BUFFERS);
	azaBuffer *buffer = &sideBufferPool[sideBuffersInUse];
	size_t *capacity = &sideBufferCapacity[sideBuffersInUse];
	uint32_t totalFrames = frames + framesLeading + framesTrailing;
	size_t capacityNeeded = totalFrames * channels;
	if (*capacity < capacityNeeded) {
		if (*capacity) {
			azaBufferDeinit(buffer, false);
		}
	}
	buffer->stride = channels;
	buffer->channelLayout.count = channels;
	buffer->samplerate = samplerate;
	if (*capacity < capacityNeeded) {
		azaBufferInit(buffer, frames, framesLeading, framesTrailing, buffer->channelLayout);
		*capacity = capacityNeeded;
	} else {
		buffer->frames = frames;
		buffer->framesLeading = framesLeading;
		buffer->framesTrailing = framesTrailing;
	}
	sideBuffersInUse++;
	return *buffer;
}

azaBuffer azaPushSideBufferZero(uint32_t frames, uint32_t framesLeading, uint32_t framesTrailing, uint32_t channels, uint32_t samplerate) {
	azaBuffer buffer = azaPushSideBuffer(frames, framesLeading, framesTrailing, channels, samplerate);
	azaBufferZero(&buffer);
	return buffer;
}

azaBuffer azaPushSideBufferCopy(azaBuffer *src) {
	azaBuffer result = azaPushSideBuffer(src->frames, src->framesLeading, src->framesTrailing, src->channelLayout.count, src->samplerate);
	azaBufferCopy(&result, src);
	return result;
}

azaBuffer azaPushSideBufferCopyZero(azaBuffer *src) {
	azaBuffer result = azaPushSideBufferZero(src->frames, src->framesLeading, src->framesTrailing, src->channelLayout.count, src->samplerate);
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