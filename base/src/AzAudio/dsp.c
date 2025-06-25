/*
	File: dsp.c
	Author: Philip Haynes
*/

#include "dsp.h"

#include "AzAudio.h"
#include "error.h"
#include "helpers.h"
#include "mixer.h"

// Good ol' MSVC causing problems like always. Never change, MSVC... never change.
#ifdef _MSC_VER
#define AZAUDIO_NO_THREADS_H
#define thread_local __declspec( thread )
#define alloca _alloca
#else
#include <alloca.h>
#endif

#include <stdlib.h>
#include <stdio.h> // snprintf
#include <string.h>
#ifndef AZAUDIO_NO_THREADS_H
#include <threads.h>
#endif
#include <assert.h>
#include <stdalign.h>

// TODO: Maybe make this dynamic, and also deal with the thread_local memory leak snafu (possibly by making it not a stack, and therefore no longer thread_local)
#define AZA_MAX_SIDE_BUFFERS 64
thread_local azaBuffer sideBufferPool[AZA_MAX_SIDE_BUFFERS] = {{0}};
thread_local size_t sideBufferCapacity[AZA_MAX_SIDE_BUFFERS] = {0};
thread_local size_t sideBuffersInUse = 0;

const char *azaDSPKindString[] = {
	"NONE",
	"User (in-place)",
	"User (transitive)",
	"Cubic Limiter",
	"RMS",
	"Lookahead Limiter",
	"Filter",
	"Compressor",
	"Delay",
	"Reverb",
	"Sampler",
	"Gate",
	"Dynamic Delay",
	"Spatialize",
};
static_assert(sizeof(azaDSPKindString) / sizeof(const char*) == AZA_DSP_KIND_COUNT, "Pls update azaDSPKindString");

const char* azaGetDSPName(azaDSP *data) {
	static char buffer[64];
	switch (data->kind) {
		case AZA_DSP_USER_SINGLE:
		case AZA_DSP_USER_DUAL:
			snprintf(buffer, sizeof(buffer), "[User] %s", ((azaDSPUser*)data)->name);
			return buffer;
		default:
			return azaDSPKindString[(uint32_t)data->kind];
	}
}

const char *azaFilterKindString[] = {
	"High Pass",
	"Low Pass",
	"Band Pass",
};
static_assert(sizeof(azaFilterKindString) / sizeof(const char*) == AZA_FILTER_KIND_COUNT, "Pls update azaFilterKindString");

azaKernel azaKernelDefaultLanczos;

azaWorld azaWorldDefault;

azaDSPRegEntries azaDSPRegistry = {0};

int azaDSPAddRegEntry(const char *name, azaDSP* (*fp_makeDSP)(uint8_t), void (*fp_freeDSP)(azaDSP*)) {
	AZA_DA_RESERVE_ONE_AT_END(azaDSPRegistry, return AZA_ERROR_OUT_OF_MEMORY);
	azaDSPRegEntry *dst = &azaDSPRegistry.data[azaDSPRegistry.count++];
	if (name) {
		aza_strcpy(dst->name, name, sizeof(dst->name));
	} else {
		memset(dst->name, 0, sizeof(dst->name));
	}
	dst->fp_makeDSP = fp_makeDSP;
	dst->fp_freeDSP = fp_freeDSP;
	return AZA_SUCCESS;
}

int azaDSPRegistryInit() {
	AZA_DA_RESERVE_COUNT(azaDSPRegistry, 11, return AZA_ERROR_OUT_OF_MEMORY);
	azaDSPAddRegEntry(azaDSPKindString[AZA_DSP_CUBIC_LIMITER], azaMakeDefaultCubicLimiter, azaFreeCubicLimiter);
	azaDSPAddRegEntry(azaDSPKindString[AZA_DSP_LOOKAHEAD_LIMITER], azaMakeDefaultLookaheadLimiter, (void(*)(azaDSP*))azaFreeLookaheadLimiter);
	azaDSPAddRegEntry(azaDSPKindString[AZA_DSP_FILTER], azaMakeDefaultFilter, (void(*)(azaDSP*))azaFreeFilter);
	azaDSPAddRegEntry(azaDSPKindString[AZA_DSP_COMPRESSOR], azaMakeDefaultCompressor, (void(*)(azaDSP*))azaFreeCompressor);
	azaDSPAddRegEntry(azaDSPKindString[AZA_DSP_GATE], azaMakeDefaultGate, (void(*)(azaDSP*))azaFreeGate);
	azaDSPAddRegEntry(azaDSPKindString[AZA_DSP_DELAY], azaMakeDefaultDelay, (void(*)(azaDSP*))azaFreeDelay);
	azaDSPAddRegEntry(azaDSPKindString[AZA_DSP_DELAY_DYNAMIC], azaMakeDefaultDelayDynamic, (void(*)(azaDSP*))azaFreeDelayDynamic);
	azaDSPAddRegEntry(azaDSPKindString[AZA_DSP_REVERB], azaMakeDefaultReverb, (void(*)(azaDSP*))azaFreeReverb);
	azaDSPAddRegEntry(azaDSPKindString[AZA_DSP_SAMPLER], azaMakeDefaultSampler, (void(*)(azaDSP*))azaFreeSampler);
	azaDSPAddRegEntry(azaDSPKindString[AZA_DSP_RMS], azaMakeDefaultRMS, (void(*)(azaDSP*))azaFreeRMS);
	azaDSPAddRegEntry(azaDSPKindString[AZA_DSP_SPATIALIZE], NULL, (void(*)(azaDSP*))azaFreeSpatialize);
	return AZA_SUCCESS;
}

bool azaFreeDSP(azaDSP *dsp) {
	const char *name = azaGetDSPName(dsp);
	for (uint32_t i = 0; i < azaDSPRegistry.count; i++) {
		if (strncmp(azaDSPRegistry.data[i].name, name, sizeof(azaDSPRegistry.data[0].name)) == 0) {
			azaDSPRegistry.data[i].fp_freeDSP(dsp);
			return true;
		}
	}
	return false;
}


azaBuffer azaPushSideBuffer(uint32_t frames, uint32_t channels, uint32_t samplerate) {
	assert(sideBuffersInUse < AZA_MAX_SIDE_BUFFERS);
	azaBuffer *buffer = &sideBufferPool[sideBuffersInUse];
	size_t *capacity = &sideBufferCapacity[sideBuffersInUse];
	size_t capacityNeeded = frames * channels;
	if (*capacity < capacityNeeded) {
		if (*capacity) {
			azaBufferDeinit(buffer);
		}
	}
	buffer->frames = frames;
	buffer->stride = channels;
	buffer->channelLayout.count = channels;
	buffer->samplerate = samplerate;
	if (*capacity < capacityNeeded) {
		azaBufferInit(buffer, buffer->frames, buffer->channelLayout);
		*capacity = capacityNeeded;
	}
	sideBuffersInUse++;
	return *buffer;
}

azaBuffer azaPushSideBufferZero(uint32_t frames, uint32_t channels, uint32_t samplerate) {
	azaBuffer buffer = azaPushSideBuffer(frames, channels, samplerate);
	memset(buffer.samples, 0, sizeof(float) * frames * channels);
	return buffer;
}

azaBuffer azaPushSideBufferCopy(azaBuffer src) {
	azaBuffer result = azaPushSideBuffer(src.frames, src.channelLayout.count, src.samplerate);
	azaBufferCopy(result, src);
	return result;
}

void azaPopSideBuffer() {
	assert(sideBuffersInUse > 0);
	sideBuffersInUse--;
}

void azaPopSideBuffers(uint8_t count) {
	assert(sideBuffersInUse >= count);
	sideBuffersInUse -= count;
}



void azaMetersUpdate(azaMeters *data, azaBuffer buffer, float inputAmp) {
	uint8_t channels = AZA_MIN((uint8_t)AZA_METERS_MAX_CHANNELS, buffer.channelLayout.count);
	for (uint8_t c = data->activeMeters; c < channels; c++) {
		data->rmsSquaredAvg[c] = 0.0f;
		data->peaks[c] = 0.0f;
		data->peaksShortTerm[c] = 0.0f;
	}
	data->activeMeters = channels;
	for (uint8_t c = 0; c < channels; c++) {
		float rmsSquaredAvg = 0.0f;
		float peak = 0.0f;
		for (uint32_t i = 0; i < buffer.frames; i++) {
			float sample = buffer.samples[i * buffer.stride + c];
			rmsSquaredAvg += azaSqrf(sample);
			sample = azaAbsf(sample);
			peak = azaMaxf(peak, sample);
		}
		rmsSquaredAvg /= (float)buffer.frames;
		rmsSquaredAvg *= azaSqrf(inputAmp);
		peak *= inputAmp;
		data->rmsSquaredAvg[c] = azaLerpf(data->rmsSquaredAvg[c], rmsSquaredAvg, (float)buffer.frames / ((float)data->rmsFrames + (float)buffer.frames));
		data->peaks[c] = azaMaxf(data->peaks[c], peak);
		data->peaksShortTerm[c] = azaMaxf(data->peaksShortTerm[c], peak);
	}
	data->rmsFrames += buffer.frames;
}



static int azaCheckBuffer(azaBuffer buffer) {
	if (buffer.samples == NULL) {
		return AZA_ERROR_NULL_POINTER;
	}
	if (buffer.channelLayout.count < 1) {
		return AZA_ERROR_INVALID_CHANNEL_COUNT;
	}
	if (buffer.frames < 1) {
		return AZA_ERROR_INVALID_FRAME_COUNT;
	}
	return AZA_SUCCESS;
}



int azaBufferInit(azaBuffer *data, uint32_t frames, azaChannelLayout channelLayout) {
	if (frames < 1) return AZA_ERROR_INVALID_FRAME_COUNT;
	if (channelLayout.count == 0) return AZA_ERROR_INVALID_CHANNEL_COUNT;
	data->frames = frames;
	data->channelLayout = channelLayout;
	data->samples = (float*)aza_calloc(data->frames * data->channelLayout.count, sizeof(float));
	if (!data->samples) return AZA_ERROR_OUT_OF_MEMORY;
	data->stride = data->channelLayout.count;
	return AZA_SUCCESS;
}

void azaBufferDeinit(azaBuffer *data) {
	aza_free(data->samples);
}

void azaBufferZero(azaBuffer buffer) {
	if (buffer.samples && buffer.frames && buffer.channelLayout.count) {
		if AZA_LIKELY(buffer.channelLayout.count == buffer.stride) {
			memset(buffer.samples, 0, sizeof(float) * buffer.frames * buffer.channelLayout.count);
		} else {
			for (uint32_t i = 0; i < buffer.frames * buffer.stride; i += buffer.stride) {
				for (uint8_t c = 0; c < buffer.channelLayout.count; c++) {
					buffer.samples[i + c] = 0.0f;
				}
			}
		}
	}
}

// azaBufferDeinterlace implementation is in specialized/azaBufferDeinterlace.c

// azaBufferReinterlace implementation is in specialized/azaBufferReinterlace.c

void azaBufferMix(azaBuffer dst, float volumeDst, azaBuffer src, float volumeSrc) {
	assert(dst.frames == src.frames);
	assert(dst.channelLayout.count == src.channelLayout.count);
	uint32_t channels = dst.channelLayout.count;
	if AZA_UNLIKELY(volumeDst == 1.0f && volumeSrc == 0.0f) {
		return;
	} else if AZA_UNLIKELY(volumeDst == 0.0f && volumeSrc == 0.0f) {
		for (uint32_t i = 0; i < dst.frames; i++) {
			for (uint32_t c = 0; c < channels; c++) {
				dst.samples[i * dst.stride + c] = 0.0f;
			}
		}
	} else if (volumeDst == 1.0f && volumeSrc == 1.0f) {
		for (uint32_t i = 0; i < dst.frames; i++) {
			for (uint32_t c = 0; c < channels; c++) {
				dst.samples[i * dst.stride + c] = dst.samples[i * dst.stride + c] + src.samples[i * src.stride + c];
			}
		}
	} else if AZA_LIKELY(volumeDst == 1.0f) {
		for (uint32_t i = 0; i < dst.frames; i++) {
			for (uint32_t c = 0; c < channels; c++) {
				dst.samples[i * dst.stride + c] = dst.samples[i * dst.stride + c] + src.samples[i * src.stride + c] * volumeSrc;
			}
		}
	} else if (volumeSrc == 1.0f) {
		for (uint32_t i = 0; i < dst.frames; i++) {
			for (uint32_t c = 0; c < channels; c++) {
				dst.samples[i * dst.stride + c] = dst.samples[i * dst.stride + c] * volumeDst + src.samples[i * src.stride + c];
			}
		}
	} else {
		for (uint32_t i = 0; i < dst.frames; i++) {
			for (uint32_t c = 0; c < channels; c++) {
				dst.samples[i * dst.stride + c] = dst.samples[i * dst.stride + c] * volumeDst + src.samples[i * src.stride + c] * volumeSrc;
			}
		}
	}
}

void azaBufferMixFade(azaBuffer dst, float volumeDstStart, float volumeDstEnd, azaBuffer src, float volumeSrcStart, float volumeSrcEnd) {
	if (volumeDstStart == volumeDstEnd && volumeSrcStart == volumeSrcEnd) {
		azaBufferMix(dst, volumeDstStart, src, volumeSrcStart);
		return;
	}
	assert(dst.frames == src.frames);
	assert(dst.channelLayout.count == src.channelLayout.count);
	uint32_t channels = dst.channelLayout.count;
	float volumeDstDelta = volumeDstEnd - volumeDstStart;
	float volumeSrcDelta = volumeSrcEnd - volumeSrcStart;
	float framesF = (float)dst.frames;
	if (volumeDstDelta == 0.0f) {
		if (volumeDstStart == 1.0f) {
			for (uint32_t i = 0; i < dst.frames; i++) {
				float t = (float)i / framesF;
				float volumeSrc = volumeSrcStart + volumeSrcDelta * t;
				for (uint32_t c = 0; c < channels; c++) {
					dst.samples[i * dst.stride + c] = dst.samples[i * dst.stride + c] + src.samples[i * src.stride + c] * volumeSrc;
				}
			}
		} else {
			for (uint32_t i = 0; i < dst.frames; i++) {
				float t = (float)i / framesF;
				float volumeSrc = volumeSrcStart + volumeSrcDelta * t;
				for (uint32_t c = 0; c < channels; c++) {
					dst.samples[i * dst.stride + c] = dst.samples[i * dst.stride + c] * volumeDstStart + src.samples[i * src.stride + c] * volumeSrc;
				}
			}
		}
	} else {
		for (uint32_t i = 0; i < dst.frames; i++) {
			float t = (float)i / framesF;
			float volumeDst = volumeDstStart + volumeDstDelta * t;
			float volumeSrc = volumeSrcStart + volumeSrcDelta * t;
			for (uint32_t c = 0; c < channels; c++) {
				dst.samples[i * dst.stride + c] = dst.samples[i * dst.stride + c] * volumeDst + src.samples[i * src.stride + c] * volumeSrc;
			}
		}
	}
}

void azaBufferCopy(azaBuffer dst, azaBuffer src) {
	assert(dst.frames == src.frames);
	assert(dst.channelLayout.count == src.channelLayout.count);
	if (dst.channelLayout.count == dst.stride && src.channelLayout.count == src.stride) {
		memcpy(dst.samples, src.samples, sizeof(float) * src.frames * src.channelLayout.count);
	} else {
		for (uint32_t i = 0; i < src.frames; i++) {
			for (uint8_t c = 0; c < src.channelLayout.count; c++) {
				dst.samples[i * dst.stride + c] = src.samples[i * src.stride + c];
			}
		}
	}
}

void azaBufferCopyChannel(azaBuffer dst, uint8_t channelDst, azaBuffer src, uint8_t channelSrc) {
	assert(dst.frames == src.frames);
	assert(channelDst < dst.channelLayout.count);
	assert(channelSrc < src.channelLayout.count);
	if (dst.stride == 1 && src.stride == 1) {
		memcpy(dst.samples, src.samples, sizeof(float) * dst.frames);
	} else if (dst.stride == 1) {
		for (uint32_t i = 0; i < dst.frames; i++) {
			dst.samples[i] = src.samples[i * src.stride + channelSrc];
		}
	} else if (src.stride == 1) {
		for (uint32_t i = 0; i < dst.frames; i++) {
			dst.samples[i * dst.stride + channelDst] = src.samples[i];
		}
	} else {
		for (uint32_t i = 0; i < dst.frames; i++) {
			dst.samples[i * dst.stride + channelDst] = src.samples[i * src.stride + channelSrc];
		}
	}
}



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

// azaBufferMixMatrix implementation is in specialized/azaBufferMixMatrix.c

int azaDSPProcessSingle(azaDSP *data, azaBuffer buffer) {
	switch (data->kind) {
		case AZA_DSP_USER_SINGLE: return azaDSPUserProcessSingle((azaDSPUser*)data, buffer);
		case AZA_DSP_CUBIC_LIMITER: return azaCubicLimiterProcess((azaCubicLimiter*)data, buffer);
		case AZA_DSP_RMS: return azaRMSProcessSingle((azaRMS*)data, buffer);
		case AZA_DSP_LOOKAHEAD_LIMITER: return azaLookaheadLimiterProcess((azaLookaheadLimiter*)data, buffer);
		case AZA_DSP_FILTER: return azaFilterProcess((azaFilter*)data, buffer);
		case AZA_DSP_COMPRESSOR: return azaCompressorProcess((azaCompressor*)data, buffer);
		case AZA_DSP_DELAY: return azaDelayProcess((azaDelay*)data, buffer);
		case AZA_DSP_REVERB: return azaReverbProcess((azaReverb*)data, buffer);
		case AZA_DSP_SAMPLER: return azaSamplerProcess((azaSampler*)data, buffer);
		case AZA_DSP_GATE: return azaGateProcess((azaGate*)data, buffer);
		case AZA_DSP_DELAY_DYNAMIC: return azaDelayDynamicProcess((azaDelayDynamic*)data, buffer, NULL);

		case AZA_DSP_SPATIALIZE:
			return AZA_ERROR_DSP_INTERFACE_NOT_GENERIC;

		case AZA_DSP_USER_DUAL:
			return AZA_ERROR_DSP_INTERFACE_EXPECTED_DUAL;

		default: return AZA_ERROR_INVALID_DSP_KIND;
	}
}

int azaDSPProcessDual(azaDSP *data, azaBuffer dst, azaBuffer src) {
	switch (data->kind) {
		case AZA_DSP_USER_DUAL: return azaDSPUserProcessDual((azaDSPUser*)data, dst, src);
		case AZA_DSP_RMS: return azaRMSProcessDual((azaRMS*)data, dst, src);

		case AZA_DSP_SPATIALIZE:
			return AZA_ERROR_DSP_INTERFACE_NOT_GENERIC;

		default: return AZA_ERROR_INVALID_DSP_KIND;
	}
}



void azaDSPUserInitSingle(azaDSPUser *data, uint32_t allocSize, const char *name, void *userdata, fp_azaProcessCallback processCallback) {
	data->header.kind = AZA_DSP_USER_SINGLE;
	data->header.metadata = azaDSPPackMetadata(allocSize, false);
	if (name) {
		aza_strcpy(data->name, name, sizeof(data->name));
	} else {
		memset(data->name, 0, sizeof(data->name));
	}
	data->userdata = userdata;
	data->processSingle = processCallback;
}

void azaDSPUserInitDual(azaDSPUser *data, uint32_t allocSize, const char *name, void *userdata, fp_azaProcessDualCallback processCallback) {
	data->header.kind = AZA_DSP_USER_DUAL;
	data->header.metadata = azaDSPPackMetadata(allocSize, false);
	if (name) {
		aza_strcpy(data->name, name, sizeof(data->name));
	} else {
		memset(data->name, 0, sizeof(data->name));
	}
	data->userdata = userdata;
	data->processDual = processCallback;
}

int azaDSPUserProcessSingle(azaDSPUser *data, azaBuffer buffer) {
	int err = data->processSingle(data->userdata, buffer);
	if AZA_UNLIKELY(err) return err;
	if (data->header.pNext) {
		return azaDSPProcessSingle(data->header.pNext, buffer);
	}
	return AZA_SUCCESS;
}

int azaDSPUserProcessDual(azaDSPUser *data, azaBuffer dst, azaBuffer src) {
	int err = data->processDual(data->userdata, dst, src);
	if AZA_UNLIKELY(err) return err;
	if (data->header.pNext) {
		return azaDSPProcessDual(data->header.pNext, dst, src);
	}
	return AZA_SUCCESS;
}



void azaOpAdd(float *lhs, float rhs) {
	*lhs += rhs;
}
void azaOpMax(float *lhs, float rhs) {
	*lhs = azaMaxf(*lhs, rhs);
}



static void azaDSPChannelDataInit(azaDSPChannelData *data, uint8_t channelCapInline, uint32_t size, uint8_t alignment) {
	data->capInline = channelCapInline;
	data->capAdditional = 0;
	data->countActive = 0;
	data->alignment = alignment;
	// NOTE: This is probably already aligned, but on the off chance that it's not, we'll handle it.
	data->size = (uint32_t)aza_align(size, alignment);
	data->additional = NULL;
}

static void azaDSPChannelDataDeinit(azaDSPChannelData *data) {
	if (data->additional) {
		aza_free(data->additional);
		data->additional = NULL;
	}
	data->capAdditional = 0;
}

static int azaEnsureChannels(azaDSPChannelData *data, uint8_t channelCount) {
	if (channelCount > data->capInline) {
		uint8_t channelCountAdditional = channelCount - data->capInline;
		if (channelCountAdditional > data->capAdditional) {
			void *newData = aza_calloc(channelCountAdditional, data->size);
			if (!newData) {
				return AZA_ERROR_OUT_OF_MEMORY;
			}
			if (data->additional) {
				memcpy(newData, data->additional, data->capAdditional * data->size);
				aza_free(data->additional);
			}
			data->additional = newData;
			data->capAdditional = channelCountAdditional;
		}
	}
	return AZA_SUCCESS;
}

static void* azaGetChannelData(azaDSPChannelData *data, uint8_t channel) {
	void *result;
	if (channel >= data->capInline) {
		channel -= data->capInline;
		assert(channel < data->capAdditional);
		result = (char*)data->additional + channel * data->size;
	} else {
		result = (void*)(aza_align((uint64_t)&data->additional + 8, data->alignment) + channel * data->size);
	}
	return result;
}



#define AZA_RMS_INLINE_BUFFER_SIZE 256

static inline uint32_t azaRMSGetBufferCapNeeded(azaRMSConfig config, uint8_t channelCapInline) {
	return (uint32_t)aza_grow(AZA_RMS_INLINE_BUFFER_SIZE, config.windowSamples * AZA_MAX(channelCapInline, 1), 32);
}

uint32_t azaRMSGetAllocSize(azaRMSConfig config, uint8_t channelCapInline) {
	size_t size = sizeof(azaRMS);
	size = azaAddSizeWithAlign(size, channelCapInline * sizeof(azaRMSChannelData), alignof(azaRMSChannelData));
	uint32_t bufferCapNeeded = azaRMSGetBufferCapNeeded(config, channelCapInline);
	if (bufferCapNeeded <= AZA_RMS_INLINE_BUFFER_SIZE) {
		size = aza_align(size + AZA_RMS_INLINE_BUFFER_SIZE * sizeof(float), alignof(azaRMS));
	}
	return (uint32_t)size;
}

void azaRMSInit(azaRMS *data, uint32_t allocSize, azaRMSConfig config, uint8_t channelCapInline) {
	data->header.kind = AZA_DSP_RMS;
	data->header.metadata = azaDSPPackMetadata(allocSize, false);
	data->config = config;
	azaDSPChannelDataInit(&data->channelData, channelCapInline, sizeof(azaRMSChannelData), alignof(azaRMSChannelData));

	data->bufferCap = azaRMSGetBufferCapNeeded(config, channelCapInline);
	if (data->bufferCap > AZA_RMS_INLINE_BUFFER_SIZE) {
		data->buffer = aza_calloc(data->bufferCap, sizeof(float));
	} else {
		data->buffer = (float*)((char*)&data->buffer + sizeof(float*) + sizeof(azaDSPChannelData) + channelCapInline * sizeof(azaRMSChannelData));
	}
}

void azaRMSDeinit(azaRMS *data) {
	azaDSPChannelDataDeinit(&data->channelData);
	if (data->bufferCap > AZA_RMS_INLINE_BUFFER_SIZE) {
		aza_free(data->buffer);
	}
}

azaRMS* azaMakeRMS(azaRMSConfig config, uint8_t channelCapInline) {
	uint32_t size = azaRMSGetAllocSize(config, channelCapInline);
	azaRMS *result = aza_calloc(1, size);
	if (result) {
		azaRMSInit(result, size, config, channelCapInline);
		azaDSPMetadataSetOwned(&result->header.metadata);
	}
	return result;
}

void azaFreeRMS(azaRMS *data) {
	azaRMSDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultRMS(uint8_t channelCapInline) {
	return (azaDSP*)azaMakeRMS((azaRMSConfig) {
		.windowSamples = 512,
		.combineOp = NULL,
	}, channelCapInline);
}

static int azaHandleRMSBuffer(azaRMS *data, uint8_t channels) {
	if (data->bufferCap < data->config.windowSamples * channels) {
		uint32_t newBufferCap = (uint32_t)aza_grow(data->bufferCap, data->config.windowSamples * channels, 32);
		float *newBuffer = aza_calloc(newBufferCap, sizeof(float));
		if (!newBuffer) {
			return AZA_ERROR_OUT_OF_MEMORY;
		}
		if (data->bufferCap > AZA_RMS_INLINE_BUFFER_SIZE) {
			aza_free(data->buffer);
		}
		data->bufferCap = newBufferCap;
		data->buffer = newBuffer;
	}
	return AZA_SUCCESS;
}

int azaRMSProcessDual(azaRMS *data, azaBuffer dst, azaBuffer src) {
	int err = AZA_SUCCESS;
	if (data == NULL) return AZA_ERROR_NULL_POINTER;
	err = azaCheckBuffer(dst);
	if AZA_UNLIKELY(err) return err;
	err = azaCheckBuffer(src);
	if AZA_UNLIKELY(err) return err;
	err = azaHandleRMSBuffer(data, 1);
	if AZA_UNLIKELY(err) return err;
	err = azaEnsureChannels(&data->channelData, 1);
	if AZA_UNLIKELY(err) return err;
	azaRMSChannelData *channelData = azaGetChannelData(&data->channelData, 0);
	float *channelBuffer = data->buffer;
	fp_azaOp op = data->config.combineOp ? data->config.combineOp : azaOpMax;
	for (size_t i = 0; i < src.frames; i++) {
		channelData->squaredSum -= channelBuffer[data->index];
		channelBuffer[data->index] = 0.0f;
		for (size_t c = 0; c < src.channelLayout.count; c++) {
			op(&channelBuffer[data->index], azaSqrf(src.samples[i * src.stride + c]));
		}
		channelData->squaredSum += channelBuffer[data->index];
		// Deal with potential rounding errors making sqrtf emit NaNs
		if (channelData->squaredSum < 0.0f) channelData->squaredSum = 0.0f;
		dst.samples[i * dst.stride] = sqrtf(channelData->squaredSum/(data->config.windowSamples * src.channelLayout.count));
		if (++data->index >= data->config.windowSamples)
			data->index = 0;
	}
	if (data->header.pNext) {
		return azaDSPProcessSingle(data->header.pNext, dst);
	}
	return AZA_SUCCESS;
}

int azaRMSProcessSingle(azaRMS *data, azaBuffer buffer) {
	int err = AZA_SUCCESS;
	if (data == NULL) return AZA_ERROR_NULL_POINTER;
	err = azaCheckBuffer(buffer);
	if AZA_UNLIKELY(err) return err;
	err = azaHandleRMSBuffer(data, buffer.channelLayout.count);
	if AZA_UNLIKELY(err) return err;
	err = azaEnsureChannels(&data->channelData, buffer.channelLayout.count);
	if AZA_UNLIKELY(err) return err;
	for (uint8_t c = 0; c < buffer.channelLayout.count; c++) {
		azaRMSChannelData *channelData = azaGetChannelData(&data->channelData, c);
		float *channelBuffer = &data->buffer[data->config.windowSamples * c];

		for (uint32_t i = 0; i < buffer.frames; i++) {
			uint32_t s = i * buffer.stride + c;
			channelData->squaredSum -= channelBuffer[data->index];
			channelBuffer[data->index] = azaSqrf(buffer.samples[s]);
			channelData->squaredSum += channelBuffer[data->index];
			// Deal with potential rounding errors making sqrtf emit NaNs
			if (channelData->squaredSum < 0.0f) channelData->squaredSum = 0.0f;

			if (++data->index >= data->config.windowSamples)
				data->index = 0;

			buffer.samples[s] = sqrtf(channelData->squaredSum/data->config.windowSamples);
		}
	}
	if (data->header.pNext) {
		return azaDSPProcessSingle(data->header.pNext, buffer);
	}
	return AZA_SUCCESS;
}



static float azaCubicLimiterSample(float sample) {
	if (sample > 1.0f)
		sample = 1.0f;
	else if (sample < -1.0f)
		sample = -1.0f;
	sample = 1.5f * sample - 0.5f * sample * sample * sample;
	return sample;
}

void azaCubicLimiterInit(azaCubicLimiter *data, uint32_t allocSize) {
	data->kind = AZA_DSP_CUBIC_LIMITER;
	data->metadata = azaDSPPackMetadata(allocSize, false);
}

azaCubicLimiter* azaMakeCubicLimiter() {
	uint32_t size = azaCubicLimiterGetAllocSize();
	azaCubicLimiter *result = aza_calloc(1, size);
	if (result) {
		azaCubicLimiterInit(result, size);
		azaDSPMetadataSetOwned(&result->metadata);
	}
	return result;
}

void azaFreeCubicLimiter(azaCubicLimiter *data) {
	aza_free(data);
}

azaDSP* azaMakeDefaultCubicLimiter(uint8_t channelCapInline) {
	return (azaDSP*)azaMakeCubicLimiter();
}

int azaCubicLimiterProcess(azaCubicLimiter *data, azaBuffer buffer) {
	int err = azaCheckBuffer(buffer);
	if AZA_UNLIKELY(err) return err;
	if (buffer.stride == buffer.channelLayout.count) {
		for (size_t i = 0; i < buffer.frames*buffer.channelLayout.count; i++) {
			buffer.samples[i] = azaCubicLimiterSample(buffer.samples[i]);
		}
	} else {
		for (size_t c = 0; c < buffer.channelLayout.count; c++) {
			for (size_t i = 0; i < buffer.frames; i++) {
				size_t s = i * buffer.stride + c;
				buffer.samples[s] = azaCubicLimiterSample(buffer.samples[s]);
			}
		}
	}
	if (data->pNext) {
		return azaDSPProcessSingle(data->pNext, buffer);
	}
	return AZA_SUCCESS;
}



uint32_t azaLookaheadLimiterGetAllocSize(uint8_t channelCapInline) {
	size_t size = sizeof(azaLookaheadLimiter);
	size = azaAddSizeWithAlign(size, channelCapInline * sizeof(azaLookaheadLimiterChannelData), alignof(azaLookaheadLimiterChannelData));
	return (uint32_t)size;
}

void azaLookaheadLimiterInit(azaLookaheadLimiter *data, uint32_t allocSize, azaLookaheadLimiterConfig config, uint8_t channelCapInline) {
	data->header.kind = AZA_DSP_LOOKAHEAD_LIMITER;
	data->header.metadata = azaDSPPackMetadata(allocSize, false);
	data->config = config;
	data->sum = 1.0f;
	data->minAmp = 1.0f;
	data->minAmpShort = 1.0f;
	azaDSPChannelDataInit(&data->channelData, channelCapInline, sizeof(azaLookaheadLimiterChannelData), alignof(azaLookaheadLimiterChannelData));
}

void azaLookaheadLimiterDeinit(azaLookaheadLimiter *data) {
	azaDSPChannelDataDeinit(&data->channelData);
}

azaLookaheadLimiter* azaMakeLookaheadLimiter(azaLookaheadLimiterConfig config, uint8_t channelCapInline) {
	uint32_t size = azaLookaheadLimiterGetAllocSize(channelCapInline);
	azaLookaheadLimiter *result = aza_calloc(1, size);
	if (result) {
		azaLookaheadLimiterInit(result, size, config, channelCapInline);
		azaDSPMetadataSetOwned(&result->header.metadata);
	}
	return result;
}

void azaFreeLookaheadLimiter(azaLookaheadLimiter *data) {
	azaLookaheadLimiterDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultLookaheadLimiter(uint8_t channelCapInline) {
	return (azaDSP*)azaMakeLookaheadLimiter((azaLookaheadLimiterConfig) {
		.gainInput = 0.0f,
		.gainOutput = 0.0f,
	}, channelCapInline);
}

int azaLookaheadLimiterProcess(azaLookaheadLimiter *data, azaBuffer buffer) {
	int err = AZA_SUCCESS;
	if (data == NULL) return AZA_ERROR_NULL_POINTER;
	err = azaCheckBuffer(buffer);
	if AZA_UNLIKELY(err) return err;
	err = azaEnsureChannels(&data->channelData, buffer.channelLayout.count);
	if AZA_UNLIKELY(err) return err;
	float amountInput = aza_db_to_ampf(data->config.gainInput);
	float amountOutput = aza_db_to_ampf(data->config.gainOutput);
	bool updateMeters = azaMixerGUIHasDSPOpen(&data->header);
	if (updateMeters) {
		azaMetersUpdate(&data->metersInput, buffer, amountInput);
	}
	// TODO: There's some odd behavior where CPU usage jumps the instant there's any attenuation and never drops again. Pls investigate!
	azaBuffer gainBuffer;
	gainBuffer = azaPushSideBuffer(buffer.frames, 1, buffer.samplerate);
	memset(gainBuffer.samples, 0, sizeof(float) * gainBuffer.frames);
	// TODO: It may be desirable to prevent the subwoofer channel from affecting the rest, and it may want its own independent limiter.
	int index = data->index;
	// Do all the gain calculations and put them into gainBuffer
	for (uint32_t i = 0; i < buffer.frames; i++) {
		for (uint8_t c = 0; c < buffer.channelLayout.count; c++) {
			float sample = azaAbsf(buffer.samples[i * buffer.stride + c]);
			gainBuffer.samples[i] = azaMaxf(sample, gainBuffer.samples[i]);
		}
		float peak = azaMaxf(gainBuffer.samples[i] * amountInput, 1.0f);
		data->peakBuffer[index] = peak;
		index = (index+1)%AZAUDIO_LOOKAHEAD_SAMPLES;
		float slope = (1.0f / peak - data->sum) / AZAUDIO_LOOKAHEAD_SAMPLES;
		if (slope < data->slope) {
			data->slope = slope;
			data->cooldown = AZAUDIO_LOOKAHEAD_SAMPLES;
		} else if (data->cooldown == 0 && data->sum < 1.0f) {
			data->slope = (1.0f - data->sum) / (AZAUDIO_LOOKAHEAD_SAMPLES * 5.0f);
			for (int index2 = 0; index2 < AZAUDIO_LOOKAHEAD_SAMPLES; index2++) {
				float peak2 = data->peakBuffer[(index+index2)%AZAUDIO_LOOKAHEAD_SAMPLES];
				float slope2 = (1.0f / peak2 - data->sum) / (float)(index2+1);
				if (slope2 < data->slope) {
					data->slope = slope2;
					data->cooldown = index2+1;
				}
			}
		} else if (data->cooldown > 0) {
			data->cooldown -= 1;
		}
		data->sum += data->slope;
		data->minAmpShort = azaMinf(data->minAmpShort, data->sum);
		if (data->sum > 1.0f) {
			data->slope = 0.0f;
			data->sum = 1.0f;
		}
		gainBuffer.samples[i] = data->sum;
	}
	data->minAmp = azaMinf(data->minAmp, data->minAmpShort);
	// Apply the gain from gainBuffer to all the channels
	for (uint8_t c = 0; c < buffer.channelLayout.count; c++) {
		azaLookaheadLimiterChannelData *channelData = azaGetChannelData(&data->channelData, c);
		index = data->index;

		for (uint32_t i = 0; i < buffer.frames; i++) {
			uint32_t s = i * buffer.stride + c;
			channelData->valBuffer[index] = buffer.samples[s];
			index = (index+1)%AZAUDIO_LOOKAHEAD_SAMPLES;
			float out = azaClampf(channelData->valBuffer[index] * gainBuffer.samples[i] * amountInput, -1.0f, 1.0f);
			buffer.samples[s] = out * amountOutput;
		}
	}
	if (updateMeters) {
		azaMetersUpdate(&data->metersOutput, buffer, 1.0f);
	}
	data->index = index;
	data->channelData.countActive = buffer.channelLayout.count;
	azaPopSideBuffer();
	if (data->header.pNext) {
		return azaDSPProcessSingle(data->header.pNext, buffer);
	}
	return AZA_SUCCESS;
}



uint32_t azaFilterGetAllocSize(uint8_t channelCapInline) {
	size_t size = sizeof(azaFilter);
	size = azaAddSizeWithAlign(size, channelCapInline * sizeof(azaFilterChannelData), alignof(azaFilterChannelData));
	return (uint32_t)size;
}

void azaFilterInit(azaFilter *data, uint32_t allocSize, azaFilterConfig config, uint8_t channelCapInline) {
	data->header.kind = AZA_DSP_FILTER;
	data->header.metadata = azaDSPPackMetadata(allocSize, false);
	data->config = config;
	azaDSPChannelDataInit(&data->channelData, channelCapInline, sizeof(azaFilterChannelData), alignof(azaFilterChannelData));
}

void azaFilterDeinit(azaFilter *data) {
	azaDSPChannelDataDeinit(&data->channelData);
}

azaFilter* azaMakeFilter(azaFilterConfig config, uint8_t channelCapInline) {
	uint32_t size = azaFilterGetAllocSize(channelCapInline);
	azaFilter *result = aza_calloc(1, size);
	if (result) {
		azaFilterInit(result, size, config, channelCapInline);
		azaDSPMetadataSetOwned(&result->header.metadata);
	}
	return result;
}

void azaFreeFilter(azaFilter *data) {
	azaFilterDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultFilter(uint8_t channelCapInline) {
	return (azaDSP*)azaMakeFilter((azaFilterConfig) {
		.kind = AZA_FILTER_LOW_PASS,
		.frequency = 500.0f,
		.dryMix = 0.0f,
	}, channelCapInline);
}

int azaFilterProcess(azaFilter *data, azaBuffer buffer) {
	int err = AZA_SUCCESS;
	if (data == NULL) return AZA_ERROR_NULL_POINTER;
	err = azaCheckBuffer(buffer);
	if AZA_UNLIKELY(err) return err;
	err = azaEnsureChannels(&data->channelData, buffer.channelLayout.count);
	if AZA_UNLIKELY(err) return err;
	float amount = azaClampf(1.0f - data->config.dryMix, 0.0f, 1.0f);
	float amountDry = azaClampf(data->config.dryMix, 0.0f, 1.0f);
	for (uint8_t c = 0; c < buffer.channelLayout.count; c++) {
		azaFilterChannelData *channelData = azaGetChannelData(&data->channelData, c);

		switch (data->config.kind) {
			case AZA_FILTER_HIGH_PASS: {
				float decay = azaClampf(expf(-AZA_TAU * (data->config.frequency / (float)buffer.samplerate)), 0.0f, 1.0f);
				for (uint32_t i = 0; i < buffer.frames; i++) {
					uint32_t s = i * buffer.stride + c;
					channelData->outputs[0] = buffer.samples[s] + decay * (channelData->outputs[0] - buffer.samples[s]);
					buffer.samples[s] = (buffer.samples[s] - channelData->outputs[0]) * amount + buffer.samples[s] * amountDry;
				}
			} break;
			case AZA_FILTER_LOW_PASS: {
				float decay = azaClampf(expf(-AZA_TAU * (data->config.frequency / (float)buffer.samplerate)), 0.0f, 1.0f);
				for (uint32_t i = 0; i < buffer.frames; i++) {
					uint32_t s = i * buffer.stride + c;
					channelData->outputs[0] = buffer.samples[s] + decay * (channelData->outputs[0] - buffer.samples[s]);
					buffer.samples[s] = channelData->outputs[0] * amount + buffer.samples[s] * amountDry;
				}
			} break;
			case AZA_FILTER_BAND_PASS: {
				float decayLow = azaClampf(expf(-AZA_TAU * (data->config.frequency / (float)buffer.samplerate)), 0.0f, 1.0f);
				float decayHigh = azaClampf(expf(-AZA_TAU * (data->config.frequency / (float)buffer.samplerate)), 0.0f, 1.0f);
				for (uint32_t i = 0; i < buffer.frames; i++) {
					uint32_t s = i * buffer.stride + c;
					channelData->outputs[0] = buffer.samples[s] + decayLow * (channelData->outputs[0] - buffer.samples[s]);
					channelData->outputs[1] = channelData->outputs[0] + decayHigh * (channelData->outputs[1] - channelData->outputs[0]);
					buffer.samples[s] = (channelData->outputs[0] - channelData->outputs[1]) * 2.0f * amount + buffer.samples[s] * amountDry;
				}
			} break;
		}
	}
	data->channelData.countActive = buffer.channelLayout.count;
	if (data->header.pNext) {
		return azaDSPProcessSingle(data->header.pNext, buffer);
	}
	return AZA_SUCCESS;
}



uint32_t azaCompressorGetAllocSize(uint8_t channelCapInline) {
	size_t size = sizeof(azaCompressor) - sizeof(azaRMS);
	size = azaAddSizeWithAlign(size, azaRMSGetAllocSize((azaRMSConfig) { 128 }, channelCapInline), alignof(azaRMS));
	return (uint32_t)size;
}

void azaCompressorInit(azaCompressor *data, uint32_t allocSize, azaCompressorConfig config, uint8_t channelCapInline) {
	data->header.kind = AZA_DSP_COMPRESSOR;
	data->header.metadata = azaDSPPackMetadata(allocSize, false);
	data->config = config;
	azaRMSConfig rmsConfig = (azaRMSConfig) {
		.windowSamples = 128,
		.combineOp = azaOpMax
	};
	azaRMSInit(&data->rms, azaRMSGetAllocSize(rmsConfig, 1), rmsConfig, 1);
}

void azaCompressorDeinit(azaCompressor *data) {
	azaRMSDeinit(&data->rms);
}

azaCompressor* azaMakeCompressor(azaCompressorConfig config, uint8_t channelCapInline) {
	uint32_t size = azaCompressorGetAllocSize(channelCapInline);
	azaCompressor *result = aza_calloc(1, size);
	if (result) {
		azaCompressorInit(result, size, config, channelCapInline);
		azaDSPMetadataSetOwned(&result->header.metadata);
	}
	return result;
}

void azaFreeCompressor(azaCompressor *data) {
	azaCompressorDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultCompressor(uint8_t channelCapInline) {
	return (azaDSP*)azaMakeCompressor((azaCompressorConfig) {
		.threshold = -12.0f,
		.ratio = 10.0f,
		.attack = 50.0f,
		.decay = 200.0f,
	}, channelCapInline);
}

int azaCompressorProcess(azaCompressor *data, azaBuffer buffer) {
	int err = AZA_SUCCESS;
	if (data == NULL) return AZA_ERROR_NULL_POINTER;
	err = azaCheckBuffer(buffer);
	if AZA_UNLIKELY(err) return err;

	bool updateMeters = azaMixerGUIHasDSPOpen(&data->header);
	if (updateMeters) {
		azaMetersUpdate(&data->metersInput, buffer, 1.0f);
	}

	azaBuffer rmsBuffer = azaPushSideBuffer(buffer.frames, 1, buffer.samplerate);
	err = azaRMSProcessDual(&data->rms, rmsBuffer, buffer);
	if AZA_UNLIKELY(err) return err;
	float t = (float)buffer.samplerate / 1000.0f;
	float attackFactor = expf(-1.0f / (data->config.attack * t));
	float decayFactor = expf(-1.0f / (data->config.decay * t));
	float overgainFactor;
	if (data->config.ratio > 1.0f) {
		overgainFactor = (1.0f - 1.0f / data->config.ratio);
	} else if (data->config.ratio < 0.0f) {
		overgainFactor = -data->config.ratio;
	} else {
		overgainFactor = 0.0f;
	}
	data->minGainShort = 0.0f;
	for (size_t i = 0; i < buffer.frames; i++) {
		float rms = aza_amp_to_dbf(rmsBuffer.samples[i]);
		if (rms < -120.0f) rms = -120.0f;
		if (rms > data->attenuation) {
			data->attenuation = rms + attackFactor * (data->attenuation - rms);
		} else {
			data->attenuation = rms + decayFactor * (data->attenuation - rms);
		}
		float gain;
		if (data->attenuation > data->config.threshold) {
			gain = overgainFactor * (data->config.threshold - data->attenuation);
		} else {
			gain = 0.0f;
		}
		data->minGainShort = azaMinf(data->minGainShort, gain);
		float amp = aza_db_to_ampf(gain + data->config.gain);
		for (size_t c = 0; c < buffer.channelLayout.count; c++) {
			size_t s = i * buffer.stride + c;
			buffer.samples[s] *= amp;
		}
	}
	data->minGain = azaMinf(data->minGain, data->minGainShort);
	azaPopSideBuffer();

	if (updateMeters) {
		azaMetersUpdate(&data->metersOutput, buffer, 1.0f);
	}

	if (data->header.pNext) {
		return azaDSPProcessSingle(data->header.pNext, buffer);
	}
	return AZA_SUCCESS;
}



uint32_t azaDelayGetAllocSize(uint8_t channelCapInline) {
	size_t size = sizeof(azaDelay);
	size = azaAddSizeWithAlign(size, channelCapInline * sizeof(azaDelayChannelData), alignof(azaDelayChannelData));
	return (uint32_t)size;
}

void azaDelayInit(azaDelay *data, uint32_t allocSize, azaDelayConfig config, uint8_t channelCapInline) {
	data->header.kind = AZA_DSP_DELAY;
	data->header.metadata = azaDSPPackMetadata(allocSize, false);
	data->config = config;
	azaDSPChannelDataInit(&data->channelData, channelCapInline, sizeof(azaDelayChannelData), alignof(azaDelayChannelData));
}

void azaDelayDeinit(azaDelay *data) {
	if (data->buffer) {
		aza_free(data->buffer);
		data->buffer = NULL;
	}
}

azaDelayChannelConfig* azaDelayGetChannelConfig(azaDelay *data, uint8_t channel) {
	azaDelayChannelData *channelData = azaGetChannelData(&data->channelData, channel);
	return &channelData->config;
}

azaDelay* azaMakeDelay(azaDelayConfig config, uint8_t channelCapInline) {
	uint32_t size = azaDelayGetAllocSize(channelCapInline);
	azaDelay *result = aza_calloc(1, size);
	if (result) {
		azaDelayInit(result, size, config, channelCapInline);
		azaDSPMetadataSetOwned(&result->header.metadata);
	}
	return result;
}

void azaFreeDelay(azaDelay *data) {
	azaDelayDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultDelay(uint8_t channelCapInline) {
	return (azaDSP*)azaMakeDelay((azaDelayConfig) {
		.gain = -6.0f,
		.gainDry = 0.0f,
		.delay = 300.0f,
		.feedback = 0.5f,
		.pingpong = 0.0f,
		.wetEffects = NULL,
	}, channelCapInline);
}

static int azaDelayHandleBufferResizes(azaDelay *data, uint32_t samplerate, uint8_t channelCount) {
	int err = AZA_SUCCESS;
	err = azaEnsureChannels(&data->channelData, channelCount);
	if AZA_UNLIKELY(err) return err;
	uint32_t delaySamplesMax = 0;
	uint32_t perChannelBufferCap = data->bufferCap / channelCount;
	uint8_t realloc = 0;
	for (uint8_t c = 0; c < channelCount; c++) {
		azaDelayChannelData *channelData = azaGetChannelData(&data->channelData, c);
		uint32_t delaySamples = (uint32_t)aza_ms_to_samples(data->config.delay + channelData->config.delay, (float)samplerate);
		if (delaySamples > delaySamplesMax) delaySamplesMax = delaySamples;
		if (channelData->delaySamples >= delaySamples) {
			if (channelData->index > delaySamples) {
				channelData->index = 0;
			}
			channelData->delaySamples = delaySamples;
		} else if (perChannelBufferCap >= delaySamples) {
			channelData->delaySamples = delaySamples;
		} else {
			realloc = 1;
		}
	}
	if (!realloc) return AZA_SUCCESS;
	// Have to realloc buffer
	uint32_t newPerChannelBufferCap = (uint32_t)aza_grow(data->bufferCap / channelCount, delaySamplesMax, 256);
	float *newBuffer = aza_calloc(sizeof(float), newPerChannelBufferCap * channelCount);
	if (!newBuffer) return AZA_ERROR_OUT_OF_MEMORY;
	for (uint8_t c = 0; c < channelCount; c++) {
		azaDelayChannelData *channelData = azaGetChannelData(&data->channelData, c);
		float *newChannelBuffer = newBuffer + c * newPerChannelBufferCap;
		if (data->buffer && channelData->delaySamples) {
			memcpy(newChannelBuffer, channelData->buffer, sizeof(float) * channelData->delaySamples);
		}
		channelData->buffer = newChannelBuffer;
		// We also have to set delaySamples since we didn't do it above
		channelData->delaySamples = (uint32_t)aza_ms_to_samples(data->config.delay + channelData->config.delay, (float)samplerate);
	}
	if (data->buffer) {
		aza_free(data->buffer);
	}
	data->buffer = newBuffer;
	return AZA_SUCCESS;
}

int azaDelayProcess(azaDelay *data, azaBuffer buffer) {
	int err = AZA_SUCCESS;
	if (data == NULL) return AZA_ERROR_NULL_POINTER;
	err = azaCheckBuffer(buffer);
	if AZA_UNLIKELY(err) return err;
	err = azaDelayHandleBufferResizes(data, buffer.samplerate, buffer.channelLayout.count);
	if AZA_UNLIKELY(err) return err;
	azaBuffer sideBuffer = azaPushSideBuffer(buffer.frames, buffer.channelLayout.count, buffer.samplerate);
	memset(sideBuffer.samples, 0, sizeof(float) * sideBuffer.frames * sideBuffer.channelLayout.count);
	for (uint8_t c = 0; c < buffer.channelLayout.count; c++) {
		azaDelayChannelData *channelData = azaGetChannelData(&data->channelData, c);
		uint32_t index = channelData->index;
		for (uint32_t i = 0; i < buffer.frames; i++) {
			uint32_t s = i * buffer.stride + c;
			uint8_t c2 = (c + 1) % buffer.channelLayout.count;
			float toAdd = buffer.samples[s] + channelData->buffer[index] * data->config.feedback;
			sideBuffer.samples[i * sideBuffer.stride + c] += toAdd * (1.0f - data->config.pingpong);
			sideBuffer.samples[i * sideBuffer.stride + c2] += toAdd * data->config.pingpong;
			index = (index+1) % channelData->delaySamples;
		}
	}
	if (data->config.wetEffects) {
		err = azaDSPProcessSingle(data->config.wetEffects, sideBuffer);
		if AZA_UNLIKELY(err) return err;
	}
	for (uint8_t c = 0; c < buffer.channelLayout.count; c++) {
		azaDelayChannelData *channelData = azaGetChannelData(&data->channelData, c);
		uint32_t index = channelData->index;
		float amount = aza_db_to_ampf(data->config.gain);
		float amountDry = aza_db_to_ampf(data->config.gainDry);
		for (uint32_t i = 0; i < buffer.frames; i++) {
			uint32_t s = i * buffer.stride + c;
			channelData->buffer[index] = sideBuffer.samples[i * sideBuffer.stride + c];
			index = (index+1) % channelData->delaySamples;
			buffer.samples[s] = channelData->buffer[index] * amount + buffer.samples[s] * amountDry;
		}
		channelData->index = index;
	}
	azaPopSideBuffer();
	if (data->header.pNext) {
		return azaDSPProcessSingle(data->header.pNext, buffer);
	}
	return AZA_SUCCESS;
}



uint32_t azaReverbGetAllocSize(uint8_t channelCapInline) {
	size_t size = sizeof(azaReverb) - sizeof(azaDelay);
	size = azaAddSizeWithAlign(size, azaDelayGetAllocSize(channelCapInline), alignof(azaDelay));
	size = azaAddSizeWithAlign(size, AZAUDIO_REVERB_DELAY_COUNT * azaDelayGetAllocSize(channelCapInline), alignof(azaDelay));
	size = azaAddSizeWithAlign(size, AZAUDIO_REVERB_DELAY_COUNT * azaFilterGetAllocSize(channelCapInline), alignof(azaFilter));
	return (uint32_t)size;
}

void azaReverbInit(azaReverb *data, uint32_t allocSize, azaReverbConfig config, uint8_t channelCapInline) {
	data->header.kind = AZA_DSP_REVERB;
	data->header.metadata = azaDSPPackMetadata(allocSize, false);
	data->config = config;

	uint32_t delayAllocSize = azaDelayGetAllocSize(channelCapInline);
	uint32_t filterAllocSize = azaFilterGetAllocSize(channelCapInline);

	azaDelayInit(&data->inputDelay, delayAllocSize, (azaDelayConfig){
		.gain = 0.0f,
		.gainDry = -INFINITY,
		.delay = config.delay,
		.feedback = 0.0f,
		.wetEffects = NULL,
		.pingpong = 0.0f,
	}, channelCapInline);

	float delays[AZAUDIO_REVERB_DELAY_COUNT] = {
		aza_samples_to_ms(2111, 48000),
		aza_samples_to_ms(2129, 48000),
		aza_samples_to_ms(2017, 48000),
		aza_samples_to_ms(2029, 48000),
		aza_samples_to_ms(1753, 48000),
		aza_samples_to_ms(1733, 48000),
		aza_samples_to_ms(1699, 48000),
		aza_samples_to_ms(1621, 48000),
		aza_samples_to_ms(1447, 48000),
		aza_samples_to_ms(1429, 48000),
		aza_samples_to_ms(1361, 48000),
		aza_samples_to_ms(1319, 48000),
		aza_samples_to_ms(1201, 48000),
		aza_samples_to_ms(1171, 48000),
		aza_samples_to_ms(1129, 48000),
		aza_samples_to_ms(1117, 48000),
		aza_samples_to_ms(1063, 48000),
		aza_samples_to_ms(1051, 48000),
		aza_samples_to_ms(1039, 48000),
		aza_samples_to_ms(1009, 48000),
		aza_samples_to_ms( 977, 48000),
		aza_samples_to_ms( 919, 48000),
		aza_samples_to_ms( 857, 48000),
		aza_samples_to_ms( 773, 48000),
		aza_samples_to_ms( 743, 48000),
		aza_samples_to_ms( 719, 48000),
		aza_samples_to_ms( 643, 48000),
		aza_samples_to_ms( 641, 48000),
		aza_samples_to_ms( 631, 48000),
		aza_samples_to_ms( 619, 48000),
	};
	for (int tap = 0; tap < AZAUDIO_REVERB_DELAY_COUNT; tap++) {
		azaDelay *delay = azaReverbGetDelayTap(data, tap);
		azaFilter *filter = azaReverbGetFilterTap(data, tap);
		azaDelayInit(delay, delayAllocSize, (azaDelayConfig) {
			.gain = 0.0f,
			.gainDry = -INFINITY,
			.delay = delays[tap],
			.feedback = 0.0f,
			.wetEffects = NULL,
			.pingpong = 0.05f,
		}, channelCapInline);
		azaFilterInit(filter, filterAllocSize, (azaFilterConfig) {
			.kind = AZA_FILTER_LOW_PASS,
			.frequency = 1000.0f,
			.dryMix = 0.0f,
		}, channelCapInline);
	}
}

void azaReverbDeinit(azaReverb *data) {
	azaDelayDeinit(&data->inputDelay);
	for (int tap = 0; tap < AZAUDIO_REVERB_DELAY_COUNT; tap++) {
		azaDelay *delay = azaReverbGetDelayTap(data, tap);
		azaFilter *filter = azaReverbGetFilterTap(data, tap);
		azaDelayDeinit(delay);
		azaFilterDeinit(filter);
	}
}

azaDelay* azaReverbGetDelayTap(azaReverb *data, int tap) {
	uint8_t channelCapInline = data->inputDelay.channelData.capInline;
	size_t size = sizeof(azaReverb) - sizeof(azaDelay);
	size = azaAddSizeWithAlign(size, azaDelayGetAllocSize(channelCapInline), alignof(azaDelay));
	size = azaAddSizeWithAlign(size, tap * azaDelayGetAllocSize(channelCapInline), alignof(azaDelay));
	azaDelay *delay = (azaDelay*)azaGetBufferOffset((char*)data, size, alignof(azaDelay));
	return delay;
}

azaFilter* azaReverbGetFilterTap(azaReverb *data, int tap) {
	uint8_t channelCapInline = data->inputDelay.channelData.capInline;
	size_t size = sizeof(azaReverb) - sizeof(azaDelay);
	size = azaAddSizeWithAlign(size, azaDelayGetAllocSize(channelCapInline), alignof(azaDelay));
	size = azaAddSizeWithAlign(size, AZAUDIO_REVERB_DELAY_COUNT * azaDelayGetAllocSize(channelCapInline), alignof(azaDelay));
	size = azaAddSizeWithAlign(size, tap * azaFilterGetAllocSize(channelCapInline), alignof(azaFilter));
	azaFilter *filter = (azaFilter*)azaGetBufferOffset((char*)data, size, alignof(azaFilter));
	return filter;
}

azaReverb* azaMakeReverb(azaReverbConfig config, uint8_t channelCapInline) {
	uint32_t size = azaReverbGetAllocSize(channelCapInline);
	azaReverb *result = aza_calloc(1, size);
	if (result) {
		azaReverbInit(result, size, config, channelCapInline);
		azaDSPMetadataSetOwned(&result->header.metadata);
	}
	return result;
}

void azaFreeReverb(azaReverb *data) {
	azaReverbDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultReverb(uint8_t channelCapInline) {
	return (azaDSP*)azaMakeReverb((azaReverbConfig) {
		.gain = -9.0f,
		.gainDry = 0.0f,
		.muteWet = false,
		.muteDry = false,
		.roomsize = 5.0f,
		.color = 1.0f,
		.delay = 50.0f,
	}, channelCapInline);
}

int azaReverbProcess(azaReverb *data, azaBuffer buffer) {
	int err = AZA_SUCCESS;
	if (data == NULL) return AZA_ERROR_NULL_POINTER;
	err = azaCheckBuffer(buffer);
	if AZA_UNLIKELY(err) return err;
	azaBuffer inputBuffer = azaPushSideBufferCopy(buffer);
	if (data->config.delay != 0.0f) {
		data->inputDelay.config.delay = data->config.delay;
		err = azaDelayProcess(&data->inputDelay, inputBuffer);
		if AZA_UNLIKELY(err) return err;
	}
	azaBuffer sideBufferCombined = azaPushSideBufferZero(buffer.frames, buffer.channelLayout.count, buffer.samplerate);
	azaBuffer sideBufferEarly = azaPushSideBuffer(buffer.frames, buffer.channelLayout.count, buffer.samplerate);
	azaBuffer sideBufferDiffuse = azaPushSideBuffer(buffer.frames, buffer.channelLayout.count, buffer.samplerate);
	float feedback = 0.985f - (0.2f / data->config.roomsize);
	float color = data->config.color * 4000.0f;
	float amount = data->config.muteWet ? 0.0f :  aza_db_to_ampf(data->config.gain);
	float amountDry = data->config.muteDry ? 0.0f : aza_db_to_ampf(data->config.gainDry);
	for (int tap = 0; tap < AZAUDIO_REVERB_DELAY_COUNT*2/3; tap++) {
		// TODO: Make feedback depend on delay time such that they all decay in amplitude at the same rate over time
		azaDelay *delay = azaReverbGetDelayTap(data, tap);
		azaFilter *filter = azaReverbGetFilterTap(data, tap);
		delay->config.feedback = feedback;
		filter->config.frequency = color;
		memcpy(sideBufferEarly.samples, inputBuffer.samples, sizeof(float) * buffer.frames * buffer.channelLayout.count);
		err = azaFilterProcess(filter, sideBufferEarly);
		if AZA_UNLIKELY(err) return err;
		err = azaDelayProcess(delay, sideBufferEarly);
		if AZA_UNLIKELY(err) return err;
		azaBufferMix(sideBufferCombined, 1.0f, sideBufferEarly, 1.0f / (float)AZAUDIO_REVERB_DELAY_COUNT);
	}
	for (int tap = AZAUDIO_REVERB_DELAY_COUNT*2/3; tap < AZAUDIO_REVERB_DELAY_COUNT; tap++) {
		azaDelay *delay = azaReverbGetDelayTap(data, tap);
		azaFilter *filter = azaReverbGetFilterTap(data, tap);
		delay->config.feedback = (float)(tap+AZAUDIO_REVERB_DELAY_COUNT) / (AZAUDIO_REVERB_DELAY_COUNT*2);
		filter->config.frequency = color*4.0f;
		memcpy(sideBufferDiffuse.samples, sideBufferCombined.samples, sizeof(float) * buffer.frames * buffer.channelLayout.count);
		azaBufferCopyChannel(sideBufferDiffuse, 0, sideBufferCombined, 0);
		err = azaFilterProcess(filter, sideBufferDiffuse);
		if AZA_UNLIKELY(err) return err;
		err = azaDelayProcess(delay, sideBufferDiffuse);
		if AZA_UNLIKELY(err) return err;
		azaBufferMix(sideBufferCombined, 1.0f, sideBufferDiffuse, 1.0f / (float)AZAUDIO_REVERB_DELAY_COUNT);
	}
	azaBufferMix(buffer, amountDry, sideBufferCombined, amount);
	azaPopSideBuffers(4);
	if (data->header.pNext) {
		return azaDSPProcessSingle(data->header.pNext, buffer);
	}
	return AZA_SUCCESS;
}



void azaSamplerInit(azaSampler *data, uint32_t allocSize, azaSamplerConfig config) {
	data->header.kind = AZA_DSP_SAMPLER;
	data->header.metadata = azaDSPPackMetadata(allocSize, false);
	data->config = config;
	data->pos.frame = 0;
	data->pos.fraction = 0.0f;
	data->s = config.speed;
	// Starting at zero ensures click-free playback no matter what
	data->g = 0.0f;
	// TODO: Probably use envelopes
}

void azaSamplerDeinit(azaSampler *data) {
	// Nothing to do :)
}

azaSampler* azaMakeSampler(azaSamplerConfig config) {
	uint32_t size = azaSamplerGetAllocSize();
	azaSampler *result = aza_calloc(1, size);
	if (result) {
		azaSamplerInit(result, size, config);
		azaDSPMetadataSetOwned(&result->header.metadata);
	}
	return result;
}

void azaFreeSampler(azaSampler *data) {
	azaSamplerDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultSampler(uint8_t channelCapInline) {
	return (azaDSP*)azaMakeSampler((azaSamplerConfig) {
		.buffer = NULL,
		.speed = 1.0f,
		.gain = 0.0f,
	});
}

int azaSamplerProcess(azaSampler *data, azaBuffer buffer) {
	int err = AZA_SUCCESS;
	if (data == NULL) return AZA_ERROR_NULL_POINTER;
	err = azaCheckBuffer(buffer);
	if AZA_UNLIKELY(err) return err;
	if (!data->config.buffer) return AZA_SUCCESS;
	if (buffer.channelLayout.count != data->config.buffer->channelLayout.count) return AZA_ERROR_MISMATCHED_CHANNEL_COUNT;
	float transition = expf(-1.0f / (AZAUDIO_SAMPLER_TRANSITION_FRAMES));
	float samplerateFactor = (float)data->config.buffer->samplerate / (float)buffer.samplerate;
	for (size_t i = 0; i < buffer.frames; i++) {
		data->s = data->config.speed + transition * (data->s - data->config.speed);
		data->g = data->config.gain + transition * (data->g - data->config.gain);

		// Adjust for different samplerates
		float speed = data->s * samplerateFactor;
		float volume = aza_db_to_ampf(data->g);

		for (uint8_t c = 0; c < buffer.channelLayout.count; c++) {
			float sample = 0.0f;
			// TODO: Maybe switch to using the lanczos kernel that we use to resample for the backend
			/* Lanczos
			int t = (int)datum->frame + (int)data->s;
			for (int i = (int)datum->frame-2; i <= t+2; i++) {
				float x = datum->frame - (float)(i);
				sample += datum->buffer->samples[i % datum->buffer->frames] * sinc(x) * sinc(x/3);
			}
			*/

			if (speed <= 1.0f) {
				// Cubic
				float abcd[4];
				int ii = data->pos.frame + (int)data->config.buffer->frames - 2;
				for (int i = 0; i < 4; i++) {
					abcd[i] = data->config.buffer->samples[(ii++ % data->config.buffer->frames) * data->config.buffer->stride + c];
				}
				sample = azaCubicf(abcd[0], abcd[1], abcd[2], abcd[3], data->pos.fraction);
			} else {
				// Oversampling
				float total = 0.0f;
				total += data->config.buffer->samples[(data->pos.frame % data->config.buffer->frames) * data->config.buffer->stride + c] * (1.0f - data->pos.fraction);
				for (int i = 1; i < (int)speed; i++) {
					total += data->config.buffer->samples[((data->pos.frame + i) % data->config.buffer->frames) * data->config.buffer->stride + c];
				}
				total += data->config.buffer->samples[((data->pos.frame + (int)speed) % data->config.buffer->frames) * data->config.buffer->stride + c] * data->pos.fraction;
				sample = total / (float)((int)speed);
			}

			/* Linear
			int t = (int)data->pos.frame + (int)data->s;
			for (int i = (int)data->pos.frame; i <= t+1; i++) {
				float x = data->pos.frame - (float)(i);
				sample += data->config.buffer->samples[i % data->config.buffer->frames] * linc(x);
			}
			*/

			buffer.samples[i * buffer.stride + c] = sample * volume;
		}
		data->pos.fraction += speed;
		uint32_t framesToAdd = (uint32_t)data->pos.fraction;
		data->pos.frame += framesToAdd;
		data->pos.fraction -= framesToAdd;
		if (data->pos.frame > data->config.buffer->frames) {
			data->pos.frame -= data->config.buffer->frames;
		}
	}
	if (data->header.pNext) {
		return azaDSPProcessSingle(data->header.pNext, buffer);
	}
	return AZA_SUCCESS;
}



uint32_t azaGateGetAllocSize() {
	size_t size = sizeof(azaGate);
	size = azaAddSizeWithAlign(size, azaRMSGetAllocSize((azaRMSConfig) { 128 }, 1), alignof(azaRMS));
	return (uint32_t)size;
}

void azaGateInit(azaGate *data, uint32_t allocSize, azaGateConfig config) {
	data->header.kind = AZA_DSP_GATE;
	data->header.metadata = azaDSPPackMetadata(allocSize, false);
	data->config = config;
	azaRMSConfig rmsConfig = (azaRMSConfig) {
		.windowSamples = 128,
		.combineOp = azaOpMax
	};
	azaRMSInit(&data->rms, azaRMSGetAllocSize(rmsConfig, 1), rmsConfig, 1);
}

void azaGateDeinit(azaGate *data) {
	azaRMSDeinit(&data->rms);
}

azaGate* azaMakeGate(azaGateConfig config) {
	uint32_t size = azaGateGetAllocSize();
	azaGate *result = aza_calloc(1, size);
	if (result) {
		azaGateInit(result, size, config);
		azaDSPMetadataSetOwned(&result->header.metadata);
	}
	return result;
}

void azaFreeGate(azaGate *data) {
	azaGateDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultGate(uint8_t channelCapInline) {
	return (azaDSP*)azaMakeGate((azaGateConfig) {
		.threshold = -18.0f,
		.attack = 5.0f,
		.decay = 100.0f,
		.activationEffects = NULL,
	});
}

int azaGateProcess(azaGate *data, azaBuffer buffer) {
	int err = AZA_SUCCESS;
	if (data == NULL) return AZA_ERROR_NULL_POINTER;
	err = azaCheckBuffer(buffer);
	if AZA_UNLIKELY(err) return err;
	azaBuffer rmsBuffer = azaPushSideBuffer(buffer.frames, 1, buffer.samplerate);
	azaBuffer activationBuffer = buffer;
	uint8_t sideBuffersInUse = 1;

	if (data->config.activationEffects) {
		activationBuffer = azaPushSideBufferCopy(buffer);
		sideBuffersInUse++;
		int err = azaDSPProcessSingle(data->config.activationEffects, activationBuffer);
		if (err) {
			azaPopSideBuffers(sideBuffersInUse);
			return err;
		}
	}

	err = azaRMSProcessDual(&data->rms, rmsBuffer, activationBuffer);
	if AZA_UNLIKELY(err) return err;
	float t = (float)buffer.samplerate / 1000.0f;
	float attackFactor = expf(-1.0f / (data->config.attack * t));
	float decayFactor = expf(-1.0f / (data->config.decay * t));

	for (size_t i = 0; i < buffer.frames; i++) {
		float rms = aza_amp_to_dbf(rmsBuffer.samples[i]);
		if (rms < -120.0f) rms = -120.0f;
		if (rms > data->config.threshold) {
			data->attenuation = rms + attackFactor * (data->attenuation - rms);
		} else {
			data->attenuation = rms + decayFactor * (data->attenuation - rms);
		}
		float gain;
		if (data->attenuation > data->config.threshold) {
			gain = 0.0f;
		} else {
			gain = -10.0f * (data->config.threshold - data->attenuation);
		}
		data->gain = gain;
		float amp = aza_db_to_ampf(gain);
		for (uint8_t c = 0; c < buffer.channelLayout.count; c++) {
			buffer.samples[i * buffer.stride + c] *= amp;
		}
	}
	azaPopSideBuffers(sideBuffersInUse);
	if (data->header.pNext) {
		return azaDSPProcessSingle(data->header.pNext, buffer);
	}
	return AZA_SUCCESS;
}



azaDelayDynamicChannelConfig* azaDelayDynamicGetChannelConfig(azaDelayDynamic *data, uint8_t channel) {
	azaDelayDynamicChannelData *channelData = azaGetChannelData(&data->channelData, channel);
	return &channelData->config;
}

static azaKernel* azaDelayDynamicGetKernel(azaDelayDynamic *data) {
	azaKernel *kernel = data->config.kernel;
	if (!kernel) {
		kernel = &azaKernelDefaultLanczos;
	}
	return kernel;
}

static int azaDelayDynamicHandleBufferResizes(azaDelayDynamic *data, azaBuffer src) {
	// TODO: Probably track channel layouts and handle them changing. Right now the buffers will break if the number of channels changes.
	int err = AZA_SUCCESS;
	err = azaEnsureChannels(&data->channelData, src.channelLayout.count);
	if AZA_UNLIKELY(err) return err;
	azaKernel *kernel = azaDelayDynamicGetKernel(data);
	uint32_t kernelSamples = kernel->length;
	uint32_t delaySamplesMax = (uint32_t)ceilf(aza_ms_to_samples(data->config.delayMax, (float)src.samplerate)) + kernelSamples;
	uint32_t totalSamplesNeeded = delaySamplesMax + src.frames;
	uint32_t perChannelBufferCap = data->bufferCap / src.channelLayout.count;
	if (perChannelBufferCap >= totalSamplesNeeded) return AZA_SUCCESS;
	// Have to realloc buffer
	uint32_t newPerChannelBufferCap = (uint32_t)aza_grow(perChannelBufferCap, totalSamplesNeeded, 256);
	float *newBuffer = aza_calloc(sizeof(float), newPerChannelBufferCap * src.channelLayout.count);
	if (!newBuffer) return AZA_ERROR_OUT_OF_MEMORY;
	for (uint8_t c = 0; c < src.channelLayout.count; c++) {
		azaDelayDynamicChannelData *channelData = azaGetChannelData(&data->channelData, c);
		float *newChannelBuffer = newBuffer + c * newPerChannelBufferCap;
		if (data->buffer) {
			memcpy(newChannelBuffer + newPerChannelBufferCap - perChannelBufferCap, channelData->buffer, sizeof(float) * perChannelBufferCap);
		}
		channelData->buffer = newChannelBuffer;
		// Maybe we don't have to do this because we probably do it in Process
		// channelData->delaySamples = aza_ms_to_samples(channelData->config.delay, (float)samplerate);
	}
	if (data->buffer) {
		aza_free(data->buffer);
	}
	data->buffer = newBuffer;
	data->bufferCap = newPerChannelBufferCap * src.channelLayout.count;
	return AZA_SUCCESS;
}

// Puts new audio data into the buffer for immediate sampling. Assumes azaDelayDynamicHandleBufferResizes was called already.
static void azaDelayDynamicPrimeBuffer(azaDelayDynamic *data, azaBuffer src) {
	azaKernel *kernel = azaDelayDynamicGetKernel(data);
	uint32_t kernelSamples = kernel->length;
	uint32_t delaySamplesMax = (uint32_t)ceilf(aza_ms_to_samples(data->config.delayMax, (float)src.samplerate)) + kernelSamples;
	for (uint8_t c = 0; c < src.channelLayout.count; c++) {
		azaDelayDynamicChannelData *channelData = azaGetChannelData(&data->channelData, c);
		// Move existing buffer back to make room for new buffer data
		// This should work because we're expecting each buffer to be at least delaySamplesMax+src.frames in size
		for (uint32_t i = 0; i < delaySamplesMax; i++) {
			channelData->buffer[i] = channelData->buffer[i+src.frames];
		}
		azaBufferCopyChannel((azaBuffer) {
			.samples = channelData->buffer + delaySamplesMax,
			.samplerate = src.samplerate,
			.frames = src.frames,
			.stride = 1,
			.channelLayout = (azaChannelLayout) { .count = 1 },
		}, 0, src, c);
	}
}

uint32_t azaDelayDynamicGetAllocSize(uint8_t channelCapInline) {
	size_t size = sizeof(azaDelayDynamic);
	size = azaAddSizeWithAlign(size, channelCapInline * sizeof(azaDelayDynamicChannelData), alignof(azaDelayDynamicChannelData));
	return (uint32_t)size;
}

int azaDelayDynamicInit(azaDelayDynamic *data, uint32_t allocSize, azaDelayDynamicConfig config, uint8_t channelCapInline, uint8_t channelCount, azaDelayDynamicChannelConfig *channelConfigs) {
	data->header.kind = AZA_DSP_DELAY_DYNAMIC;
	data->header.metadata = azaDSPPackMetadata(allocSize, false);
	data->config = config;
	azaDSPChannelDataInit(&data->channelData, channelCapInline, sizeof(azaDelayDynamicChannelData), alignof(azaDelayDynamicChannelData));
	int err = azaEnsureChannels(&data->channelData, channelCount);
	if AZA_UNLIKELY(err) return err;
	if (channelConfigs) {
		for (uint8_t c = 0; c < channelCount; c++) {
			azaDelayDynamicChannelConfig *channelConfig = azaDelayDynamicGetChannelConfig(data, c);
			*channelConfig = channelConfigs[c];
		}
	}
	return AZA_SUCCESS;
}

void azaDelayDynamicDeinit(azaDelayDynamic *data) {
	if (data->buffer) {
		aza_free(data->buffer);
	}
	data->buffer = NULL;
	data->bufferCap = 0;
}

azaDelayDynamic* azaMakeDelayDynamic(azaDelayDynamicConfig config, uint8_t channelCapInline, uint8_t channelCount, azaDelayDynamicChannelConfig *channelConfigs) {
	uint32_t size = azaDelayDynamicGetAllocSize(channelCapInline);
	azaDelayDynamic *result = aza_calloc(1, size);
	if (result) {
		int err = azaDelayDynamicInit(result, size, config, channelCapInline, channelCount, channelConfigs);
		if (err) {
			aza_free(result);
			return NULL;
		}
		azaDSPMetadataSetOwned(&result->header.metadata);
	}
	return result;
}

void azaFreeDelayDynamic(azaDelayDynamic *data) {
	azaDelayDynamicDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultDelayDynamic(uint8_t channelCapInline) {
	return (azaDSP*)azaMakeDelayDynamic((azaDelayDynamicConfig) {
		.gain = -6.0f,
		.gainDry = 0.0f,
		.delayMax = 500.0f,
		.feedback = 0.5f,
		.pingpong = 0.0f,
		.wetEffects = NULL,
		.kernel = NULL,
	}, channelCapInline, channelCapInline, NULL);
}

int azaDelayDynamicProcess(azaDelayDynamic *data, azaBuffer buffer, float *endChannelDelays) {
	int err = AZA_SUCCESS;
	uint8_t numSideBuffers = 0;
	if (data == NULL) return AZA_ERROR_NULL_POINTER;
	err = azaCheckBuffer(buffer);
	if AZA_UNLIKELY(err) return err;
	azaKernel *kernel = azaDelayDynamicGetKernel(data);
	azaBuffer inputBuffer;
	if (data->config.wetEffects) {
		inputBuffer = azaPushSideBufferCopy(buffer);
		numSideBuffers++;
		err = azaDSPProcessSingle(data->config.wetEffects, inputBuffer);
		if (err) goto error;
	} else {
		inputBuffer = buffer;
	}
	err = azaDelayDynamicHandleBufferResizes(data, inputBuffer);
	if (err) goto error;
	// TODO: Verify whether this matters. It was difficult to tell whether there was any problem with not factoring in the kernel (which I suppose would only matter for very close to 0 delay).
	int kernelSamplesLeft = kernel->sampleZero;
	int kernelSamplesRight = kernel->length - kernel->sampleZero;
	uint32_t delaySamplesMax = (uint32_t)ceilf(aza_ms_to_samples(data->config.delayMax, (float)buffer.samplerate));
	for (uint8_t c = 0; c < inputBuffer.channelLayout.count; c++) {
		azaDelayDynamicChannelData *channelData = azaGetChannelData(&data->channelData, c);
		float startIndex = (float)delaySamplesMax - aza_ms_to_samples(channelData->config.delay, (float)inputBuffer.samplerate);
		float endIndex = startIndex + (float)inputBuffer.frames;
		if (endChannelDelays) {
			endIndex -= aza_ms_to_samples(endChannelDelays[c] - channelData->config.delay, (float)inputBuffer.samplerate);
		}
		startIndex = azaClampf(startIndex, 0.0f, (float)delaySamplesMax);
		endIndex = azaClampf(endIndex, 0.0f, (float)delaySamplesMax);
		if (startIndex >= endIndex) continue;
		uint8_t c2 = (c + 1) % inputBuffer.channelLayout.count;
		for (uint32_t i = 0; i < inputBuffer.frames; i++) {
			float index = azaLerpf(startIndex, endIndex, (float)i / (float)inputBuffer.frames);
			uint32_t s = i * inputBuffer.stride + c;
			float toAdd = inputBuffer.samples[s];
			if (data->config.feedback != 0.0f) {
			 	toAdd += azaSampleWithKernel(channelData->buffer+kernelSamplesLeft, 1, -kernelSamplesLeft, delaySamplesMax+kernelSamplesRight+inputBuffer.frames, kernel, index) * data->config.feedback;
			}
			inputBuffer.samples[i * inputBuffer.stride + c] += toAdd * (1.0f - data->config.pingpong);
			inputBuffer.samples[i * inputBuffer.stride + c2] += toAdd * data->config.pingpong;
		}
	}
	azaDelayDynamicPrimeBuffer(data, inputBuffer);
	for (uint8_t c = 0; c < buffer.channelLayout.count; c++) {
		azaDelayDynamicChannelData *channelData = azaGetChannelData(&data->channelData, c);
		float startIndex = (float)delaySamplesMax - aza_ms_to_samples(channelData->config.delay, (float)buffer.samplerate);
		float endIndex = startIndex + (float)buffer.frames;
		if (endChannelDelays) {
			endIndex -= aza_ms_to_samples(endChannelDelays[c] - channelData->config.delay, (float)buffer.samplerate);
			channelData->config.delay = endChannelDelays[c];
		}
		startIndex = azaClampf(startIndex, 0.0f, (float)(delaySamplesMax + buffer.frames));
		endIndex = azaClampf(endIndex, 0.0f, (float)(delaySamplesMax + buffer.frames));
		float amount = aza_db_to_ampf(data->config.gain);
		float amountDry = aza_db_to_ampf(data->config.gainDry);
		if (startIndex >= endIndex) amount = 0.0f;
		for (uint32_t i = 0; i < buffer.frames; i++) {
			float index = azaLerpf(startIndex, endIndex, (float)i / (float)buffer.frames);
			uint32_t s = i * buffer.stride + c;
			float wet = azaSampleWithKernel(channelData->buffer+kernelSamplesLeft, 1, -kernelSamplesLeft, delaySamplesMax+kernelSamplesRight+buffer.frames, kernel, index);
			buffer.samples[s] = wet * amount + buffer.samples[s] * amountDry;
		}
	}
	if (data->header.pNext) {
		err = azaDSPProcessSingle(data->header.pNext, buffer);
	}
error:
	azaPopSideBuffers(numSideBuffers);
	return err;
}



int azaKernelInit(azaKernel *kernel, uint32_t length, uint32_t sampleZero, uint32_t scale) {
	kernel->length = length;
	kernel->sampleZero = sampleZero;
	kernel->scale = scale;
	kernel->size = length * scale;
	kernel->table = aza_calloc(kernel->size, sizeof(float));
	if (!kernel->table) return AZA_ERROR_OUT_OF_MEMORY;
	uint32_t packedSize = length * (scale+1);
	kernel->packed = aza_calloc(packedSize, sizeof(float));
	if (!kernel->packed) {
		aza_free(kernel->table);
		return AZA_ERROR_OUT_OF_MEMORY;
	}
	return AZA_SUCCESS;
}

void azaKernelDeinit(azaKernel *kernel) {
	aza_free(kernel->table);
	aza_free(kernel->packed);
}

void azaKernelPack(azaKernel *kernel) {
	assert(kernel->table);
	assert(kernel->packed);
	for (uint32_t subsample = 0; subsample <= kernel->scale; subsample++) {
		float *dst = kernel->packed + (subsample * kernel->length);
		float *src = kernel->table + subsample;
		for (uint32_t i = 0; i < kernel->length; i++) {
			dst[i] = src[i*kernel->scale];
		}
	}
}

// azaKernelSample and azaSampleWithKernel are implemented in specialized/azaKernel.c

int azaKernelMakeLanczos(azaKernel *kernel, uint32_t resolution, uint32_t radius) {
	int err = azaKernelInit(kernel, 1+radius*2, 1+radius, resolution);
	if (err) return err;
	kernel->table[0] = 0.0f;
	for (uint32_t i = 0; i < radius * resolution; i++) {
		float value = azaLanczosf((float)i / (float)resolution, radius);
		kernel->table[kernel->sampleZero*resolution - i] = value;
		kernel->table[kernel->sampleZero*resolution + i] = value;
	}
	kernel->table[kernel->size-1] = 0.0f;
	azaKernelPack(kernel);
	return AZA_SUCCESS;
}

void azaResample(azaKernel *kernel, float factor, float *dst, int dstStride, int dstFrames, float *src, int srcStride, int srcFrameMin, int srcFrameMax, float srcSampleOffset) {
	for (uint32_t i = 0; i < (uint32_t)dstFrames; i++) {
		float pos = (float)i * factor + srcSampleOffset;
		dst[i * dstStride] = azaSampleWithKernel(src, srcStride, srcFrameMin, srcFrameMax, kernel, pos);
	}
}

void azaResampleAdd(azaKernel *kernel, float factor, float amp, float *dst, int dstStride, int dstFrames, float *src, int srcStride, int srcFrameMin, int srcFrameMax, float srcSampleOffset) {
	for (uint32_t i = 0; i < (uint32_t)dstFrames; i++) {
		float pos = (float)i * factor + srcSampleOffset;
		dst[i * dstStride] += amp * azaSampleWithKernel(src, srcStride, srcFrameMin, srcFrameMax, kernel, pos);
	}
}

#define PRINT_CHANNEL_AMPS 0
#define PRINT_CHANNEL_DELAYS 0

#if PRINT_CHANNEL_AMPS || PRINT_CHANNEL_DELAYS
static int repeatCount = 0;
#endif

struct channelMetadata {
	float amp;
	uint32_t channel;
};

int compareChannelMetadataAmp(const void *_lhs, const void *_rhs) {
	const struct channelMetadata *lhs = _lhs;
	const struct channelMetadata *rhs = _rhs;
	if (lhs->amp == rhs->amp) return 0;
	// We want descending order
	if (lhs->amp < rhs->amp) return 1;
	return -1;
};
int compareChannelMetadataChannel(const void *_lhs, const void *_rhs) {
	const struct channelMetadata *lhs = _lhs;
	const struct channelMetadata *rhs = _rhs;
	if (lhs->channel == rhs->channel) return 0;
	// We want ascending order
	if (lhs->channel < rhs->channel) return -1;
	return 1;
};


static void azaGatherChannelPresenseMetadata(azaChannelLayout channelLayout, uint8_t *hasFront, uint8_t *hasMidFront, uint8_t *hasSub, uint8_t *hasBack, uint8_t *hasSide, uint8_t *hasAerials, uint8_t *subChannel) {
	for (uint8_t i = 0; i < channelLayout.count; i++) {
		switch (channelLayout.positions[i]) {
			case AZA_POS_LEFT_FRONT:
			case AZA_POS_CENTER_FRONT:
			case AZA_POS_RIGHT_FRONT:
				*hasFront = 1;
				break;
			case AZA_POS_LEFT_CENTER_FRONT:
			case AZA_POS_RIGHT_CENTER_FRONT:
				*hasMidFront = 1;
				break;
			case AZA_POS_SUBWOOFER:
				*hasSub = 1;
				*subChannel = i;
				break;
			case AZA_POS_LEFT_BACK:
			case AZA_POS_CENTER_BACK:
			case AZA_POS_RIGHT_BACK:
				*hasBack = 1;
				break;
			case AZA_POS_LEFT_SIDE:
			case AZA_POS_RIGHT_SIDE:
				*hasSide = 1;
				break;
			case AZA_POS_CENTER_TOP:
				*hasAerials = 1;
				break;
			case AZA_POS_LEFT_FRONT_TOP:
			case AZA_POS_CENTER_FRONT_TOP:
			case AZA_POS_RIGHT_FRONT_TOP:
				*hasFront = 1;
				*hasAerials = 1;
				break;
			case AZA_POS_LEFT_BACK_TOP:
			case AZA_POS_CENTER_BACK_TOP:
			case AZA_POS_RIGHT_BACK_TOP:
				*hasBack = 1;
				*hasAerials = 1;
				break;
		}
	}
}

static void azaGetChannelMetadata(azaChannelLayout channelLayout, azaVec3 *dstVectors, uint8_t *nonSubChannels, uint8_t *hasAerials) {
	uint8_t hasFront = 0, hasMidFront = 0, hasSub = 0, hasBack = 0, hasSide = 0, subChannel = 0;
	*hasAerials = 0;
	azaGatherChannelPresenseMetadata(channelLayout, &hasFront, &hasMidFront, &hasSub, &hasBack, &hasSide, hasAerials, &subChannel);
	*nonSubChannels = hasSub ? channelLayout.count-1 : channelLayout.count;
	// Angles are relative to front center, to be signed later
	// These relate to anglePhi above
	float angleFront = AZA_DEG_TO_RAD(75.0f), angleMidFront = AZA_DEG_TO_RAD(30.0f), angleSide = AZA_DEG_TO_RAD(90.0f), angleBack = AZA_DEG_TO_RAD(130.0f);
	if (hasFront && hasMidFront && hasSide && hasBack) {
		// Standard 8 or 9 speaker layout
		angleFront = AZA_DEG_TO_RAD(60.0f);
		angleMidFront = AZA_DEG_TO_RAD(30.0f);
		angleBack = AZA_DEG_TO_RAD(140.0f);
	} else if (hasFront && hasSide && hasBack) {
		// Standard 6 or 7 speaker layout
		angleFront = AZA_DEG_TO_RAD(60.0f);
		angleBack = AZA_DEG_TO_RAD(140.0f);
	} else if (hasFront && hasBack) {
		// Standard 4 or 5 speaker layout
		angleFront = AZA_DEG_TO_RAD(60.0f);
		angleBack = AZA_DEG_TO_RAD(115.0f);
	} else if (hasFront) {
		// Standard 2 or 3 speaker layout
		angleFront = AZA_DEG_TO_RAD(75.0f);
	} else if (hasBack) {
		// Weird, will probably never actually happen, but we can work with it
		angleBack = AZA_DEG_TO_RAD(110.0f);
	} else {
		// We're confused, just do anything
		angleFront = AZA_DEG_TO_RAD(45.0f);
		angleMidFront = AZA_DEG_TO_RAD(22.5f);
		angleSide = AZA_DEG_TO_RAD(90.0f);
		angleBack = AZA_DEG_TO_RAD(120.0f);
	}
	for (uint8_t i = 0; i < channelLayout.count; i++) {
		switch (channelLayout.positions[i]) {
			case AZA_POS_LEFT_FRONT:
				dstVectors[i] = (azaVec3) { sinf(-angleFront), 0.0f, cosf(-angleFront) };
				break;
			case AZA_POS_CENTER_FRONT:
				dstVectors[i] = (azaVec3) { 0.0f, 0.0f, 1.0f };
				break;
			case AZA_POS_RIGHT_FRONT:
				dstVectors[i] = (azaVec3) { sinf(angleFront), 0.0f, cosf(angleFront) };
				break;
			case AZA_POS_LEFT_CENTER_FRONT:
				dstVectors[i] = (azaVec3) { sinf(-angleMidFront), 0.0f, cosf(-angleMidFront) };
				break;
			case AZA_POS_RIGHT_CENTER_FRONT:
				dstVectors[i] = (azaVec3) { sinf(angleMidFront), 0.0f, cosf(angleMidFront) };
				break;
			case AZA_POS_LEFT_BACK:
				dstVectors[i] = (azaVec3) { sinf(-angleBack), 0.0f, cosf(-angleBack) };
				break;
			case AZA_POS_CENTER_BACK:
				dstVectors[i] = (azaVec3) { 0.0f, 0.0f, -1.0f };
				break;
			case AZA_POS_RIGHT_BACK:
				dstVectors[i] = (azaVec3) { sinf(angleBack), 0.0f, cosf(angleBack) };
				break;
			case AZA_POS_LEFT_SIDE:
				dstVectors[i] = (azaVec3) { sinf(-angleSide), 0.0f, cosf(-angleSide) };
				break;
			case AZA_POS_RIGHT_SIDE:
				dstVectors[i] = (azaVec3) { sinf(angleSide), 0.0f, cosf(angleSide) };
				break;
			case AZA_POS_CENTER_TOP:
				dstVectors[i] = (azaVec3) { 0.0f, 1.0f, 0.0f };
				break;
			case AZA_POS_LEFT_FRONT_TOP:
				dstVectors[i] = azaVec3Normalized((azaVec3) { sinf(-angleFront), 1.0f, cosf(-angleFront) });
				break;
			case AZA_POS_CENTER_FRONT_TOP:
				dstVectors[i] = azaVec3Normalized((azaVec3) { 0.0f, 1.0f, 1.0f });
				break;
			case AZA_POS_RIGHT_FRONT_TOP:
				dstVectors[i] = azaVec3Normalized((azaVec3) { sinf(angleFront), 1.0f, cosf(angleFront) });
				break;
			case AZA_POS_LEFT_BACK_TOP:
				dstVectors[i] = azaVec3Normalized((azaVec3) { sinf(-angleBack), 1.0f, cosf(-angleBack) });
				break;
			case AZA_POS_CENTER_BACK_TOP:
				dstVectors[i] = azaVec3Normalized((azaVec3) { 0.0f, 1.0f, -1.0f });
				break;
			case AZA_POS_RIGHT_BACK_TOP:
				dstVectors[i] = azaVec3Normalized((azaVec3) { sinf(angleBack), 1.0f, cosf(angleBack) });
				break;
			default: // This includes AZA_POS_SUBWOOFER
				continue;
		}
	}
}

static uint32_t azaSpatializeChannelDataGetAllocSize(uint8_t channelCapInline) {
	return channelCapInline * azaFilterGetAllocSize(1);
}

uint32_t azaSpatializeGetAllocSize(uint8_t channelCapInline) {
	size_t size = sizeof(azaSpatialize);
	size = azaAddSizeWithAlign(size, azaSpatializeChannelDataGetAllocSize(channelCapInline), alignof(azaSpatializeChannelData));
	size = azaAddSizeWithAlign(size, azaDelayDynamicGetAllocSize(channelCapInline), alignof(azaDelayDynamic));
	return (uint32_t)size;
}

void azaSpatializeInit(azaSpatialize *data, uint32_t allocSize, azaSpatializeConfig config, uint8_t channelCapInline) {
	uint32_t filterAllocSize = azaFilterGetAllocSize(1);
	data->header.kind = AZA_DSP_SPATIALIZE;
	data->header.metadata = azaDSPPackMetadata(allocSize, false);
	data->config = config;
	azaDSPChannelDataInit(&data->channelData, channelCapInline, filterAllocSize, alignof(azaSpatializeChannelData));
	for (uint8_t c = 0; c < channelCapInline; c++) {
		azaSpatializeChannelData *channelData = azaGetChannelData(&data->channelData, c);
		azaFilterInit(&channelData->filter, filterAllocSize, (azaFilterConfig) {
			.kind = AZA_FILTER_LOW_PASS,
			.dryMix = 0.0f,
			.frequency = 15000.0f,
		}, 1);
	}
	azaDelayDynamic *delay = azaSpatializeGetDelayDynamic(data);
	// NOTE: We can ignore the return value because channelCount == channelCapInline
	azaDelayDynamicInit(delay, azaDelayDynamicGetAllocSize(channelCapInline), (azaDelayDynamicConfig){
		.gain = 0.0f,
		.gainDry = -INFINITY,
		.delayMax = config.delayMax != 0.0f ? config.delayMax : 500.0f,
		.feedback = 0.0f,
		.pingpong = 0.0f,
		.wetEffects = NULL,
		.kernel = NULL,
	}, channelCapInline, channelCapInline, NULL);
}

void azaSpatializeDeinit(azaSpatialize *data) {
	for (uint8_t c = 0; c < data->channelData.capInline + data->channelData.capAdditional; c++) {
		azaSpatializeChannelData *channelData = azaGetChannelData(&data->channelData, c);
		azaFilterDeinit(&channelData->filter);
	}
	azaDelayDynamic *delay = azaSpatializeGetDelayDynamic(data);
	azaDelayDynamicDeinit(delay);
}

azaDelayDynamic* azaSpatializeGetDelayDynamic(azaSpatialize *data) {
	azaDelayDynamic *result = (azaDelayDynamic*)azaGetBufferOffset((char*)data, sizeof(azaSpatialize) + data->channelData.size * data->channelData.capInline, alignof(azaDelayDynamic));
	return result;
}

azaSpatialize* azaMakeSpatialize(azaSpatializeConfig config, uint8_t channelCapInline) {
	uint32_t size = azaSpatializeGetAllocSize(channelCapInline);
	azaSpatialize *result = aza_calloc(1, size);
	if (result) {
		azaSpatializeInit(result, size, config, channelCapInline);
		azaDSPMetadataSetOwned(&result->header.metadata);
	}
	return result;
}

void azaFreeSpatialize(azaSpatialize *data) {
	azaSpatializeDeinit(data);
	aza_free(data);
}

static float azaSpatializeGetFilterCutoff(float delay, float dot) {
	return 192000.0f / azaMaxf(delay, 1.0f) * (dot * 0.35f + 0.65f);
}

int azaSpatializeProcess(azaSpatialize *data, azaBuffer dstBuffer, azaBuffer srcBuffer, azaVec3 srcPosStart, float srcAmpStart, azaVec3 srcPosEnd, float srcAmpEnd) {
	int err = AZA_SUCCESS;
	err = azaCheckBuffer(dstBuffer);
	if AZA_UNLIKELY(err) return err;
	err = azaCheckBuffer(srcBuffer);
	if AZA_UNLIKELY(err) return err;
	if (dstBuffer.samplerate != srcBuffer.samplerate) return AZA_ERROR_MISMATCHED_SAMPLERATE;
	if (dstBuffer.frames != srcBuffer.frames) return AZA_ERROR_MISMATCHED_FRAME_COUNT;
	if (srcBuffer.channelLayout.count != 1) return AZA_ERROR_INVALID_CHANNEL_COUNT;
	// TODO: Should this just be an error?
	if (dstBuffer.channelLayout.count > AZA_MAX_CHANNEL_POSITIONS) dstBuffer.channelLayout.count = AZA_MAX_CHANNEL_POSITIONS;
	const azaWorld *world = data->config.world;
	if (world == NULL) {
		world = &azaWorldDefault;
	}
	// Transform srcPos to headspace
	srcPosStart = azaMulVec3Mat3(azaSubVec3(srcPosStart, world->origin), world->orientation);
	srcPosEnd = azaMulVec3Mat3(azaSubVec3(srcPosEnd, world->origin), world->orientation);
	azaVec3 srcNormalStart;
	azaVec3 srcNormalEnd;

	azaBuffer sideBuffer = azaPushSideBufferZero(dstBuffer.frames, dstBuffer.channelLayout.count, dstBuffer.samplerate);
	float delayStart = azaVec3Norm(srcPosStart) / world->speedOfSound * 1000.0f;
	float delayEnd = azaVec3Norm(srcPosEnd) / world->speedOfSound * 1000.0f;
	if (dstBuffer.channelLayout.count == 1) {
		// Nothing to do but put it in there I guess
		azaBufferMixFade(sideBuffer, 1.0f, 1.0f, srcBuffer, srcAmpStart, srcAmpEnd);
		if (data->config.mode == AZA_SPATIALIZE_ADVANCED) {
			// Gotta do the doppler
			azaDelayDynamic *delay = azaSpatializeGetDelayDynamic(data);
			err = azaEnsureChannels(&data->channelData, 1);
			if AZA_UNLIKELY(err) return err;
			azaSpatializeChannelData *channelData = azaGetChannelData(&data->channelData, 0);
			err = azaEnsureChannels(&delay->channelData, 1);
			if AZA_UNLIKELY(err) return err;
			azaDelayDynamicChannelConfig *channelConfig = azaDelayDynamicGetChannelConfig(delay, 0);
			channelConfig->delay = delayStart;
			err = azaDelayDynamicProcess(delay, sideBuffer, &delayEnd);
			if AZA_UNLIKELY(err) return err;
			channelData->filter.config.frequency = azaSpatializeGetFilterCutoff(delayStart, 1.0f);
			err = azaFilterProcess(&channelData->filter, sideBuffer);
			if AZA_UNLIKELY(err) return err;
		}
		azaBufferMix(dstBuffer, 1.0f, sideBuffer, 1.0f);
		azaPopSideBuffer();
		return AZA_SUCCESS;
	}
	// How much of the signal to add to all channels in case srcPos is crossing close to the head
	float allChannelAddAmpStart = 0.0f;
	float allChannelAddAmpEnd = 0.0f;
	float normStart, normEnd;
	{
		normStart = azaVec3Norm(srcPosStart);
		if (normStart < 0.5f) {
			allChannelAddAmpStart = (0.5f - normStart) * 2.0f;
			srcNormalStart = srcPosStart;
		} else {
			srcNormalStart = azaDivVec3Scalar(srcPosStart, normStart);
		}
		normEnd = azaVec3Norm(srcPosEnd);
		if (normEnd < 0.5f) {
			allChannelAddAmpEnd = (0.5f - normEnd) * 2.0f;
			srcNormalEnd = srcPosEnd;
		} else {
			srcNormalEnd = azaDivVec3Scalar(srcPosEnd, normEnd);
		}
	}

	uint8_t nonSubChannels, hasAerials;
	azaVec3 channelVectors[AZA_MAX_CHANNEL_POSITIONS];
	azaGetChannelMetadata(dstBuffer.channelLayout, channelVectors, &nonSubChannels, &hasAerials);
	float channelDelayStart[AZA_MAX_CHANNEL_POSITIONS];
	float channelDelayEnd[AZA_MAX_CHANNEL_POSITIONS];
	float channelDot[AZA_MAX_CHANNEL_POSITIONS];

	// Position our channel vectors
	struct channelMetadata channelsStart[AZA_MAX_CHANNEL_POSITIONS];
	struct channelMetadata channelsEnd[AZA_MAX_CHANNEL_POSITIONS];
	memset(channelsStart, 0, sizeof(channelsStart));
	memset(channelsEnd, 0, sizeof(channelsEnd));
	float totalMagnitudeStart = 0.0f;
	float totalMagnitudeEnd = 0.0f;
	float earDistance = data->config.earDistance;
	if (earDistance == 0.0f) {
		earDistance = 0.085f;
	}
	for (uint8_t i = 0; i < dstBuffer.channelLayout.count; i++) {
		channelsStart[i].channel = i;
		channelsEnd[i].channel = i;
		channelDot[i] = azaVec3Dot(channelVectors[i], srcNormalStart);
		channelsStart[i].amp = 0.5f * normStart + 0.5f * channelDot[i] + allChannelAddAmpStart / (float)nonSubChannels;
		channelsEnd[i].amp = 0.5f * normEnd + 0.5f * azaVec3Dot(channelVectors[i], srcNormalEnd) + allChannelAddAmpEnd / (float)nonSubChannels;
		azaVec3 earPos = azaMulVec3Scalar(channelVectors[i], earDistance);
		if (data->config.mode == AZA_SPATIALIZE_ADVANCED) {
			// channelDelayStart[i] = 0.01f;
			// channelDelayEnd[i] = 0.01f;
			channelDelayStart[i] = azaVec3Norm(azaSubVec3(srcPosStart, earPos)) / world->speedOfSound * 1000.0f;
			channelDelayEnd[i] = azaVec3Norm(azaSubVec3(srcPosEnd, earPos)) / world->speedOfSound * 1000.0f;
		} else {
			channelDelayStart[i] = delayStart;
			channelDelayEnd[i] = delayEnd;
		}
		// channelsStart[i].amp = azaVec3Dot(channelVectors[i], srcNormalStart) + allChannelAddAmpStart / (float)nonSubChannels;
		// channelsEnd[i].amp = azaVec3Dot(channelVectors[i], srcNormalEnd) + allChannelAddAmpEnd / (float)nonSubChannels;
		// if (channelsStart[i].amp < 0.0f) channelsStart[i].amp = 0.0f;
		// if (channelsEnd[i].amp < 0.0f) channelsEnd[i].amp = 0.0f;
		// channelsStart[i].amp = 0.25f + 0.75f * channelsStart[i].amp;
		// channelsEnd[i].amp = 0.25f + 0.75f * channelsEnd[i].amp;
		totalMagnitudeStart += channelsStart[i].amp;
		totalMagnitudeEnd += channelsEnd[i].amp;
	}

	float minAmp = dstBuffer.channelLayout.formFactor == AZA_FORM_FACTOR_HEADPHONES ? 0.75f : 0.0f;

	if (dstBuffer.channelLayout.count > 2) {
		int minChannel = 2;
		if (dstBuffer.channelLayout.count > 3 && hasAerials) {
			// TODO: This probably isn't a reliable way to use aerials. Probably do something smarter.
			minChannel = 3;
		}
		// Get channel amps in descending order
		qsort(channelsStart, dstBuffer.channelLayout.count, sizeof(struct channelMetadata), compareChannelMetadataAmp);
		qsort(channelsEnd, dstBuffer.channelLayout.count, sizeof(struct channelMetadata), compareChannelMetadataAmp);

		float ampMaxRangeStart = channelsStart[0].amp;
		float ampMaxRangeEnd = channelsEnd[0].amp;
		float ampMinRangeStart = channelsStart[minChannel].amp;
		float ampMinRangeEnd = channelsEnd[minChannel].amp;
		totalMagnitudeStart = 0.0f;
		totalMagnitudeEnd = 0.0f;
		for (uint8_t i = 0; i < dstBuffer.channelLayout.count; i++) {
			channelsStart[i].amp = azaLinstepf(channelsStart[i].amp, ampMinRangeStart, ampMaxRangeStart) + allChannelAddAmpStart / (float)nonSubChannels;
			channelsEnd[i].amp = azaLinstepf(channelsEnd[i].amp, ampMinRangeEnd, ampMaxRangeEnd) + allChannelAddAmpEnd / (float)nonSubChannels;
			totalMagnitudeStart += channelsStart[i].amp;
			totalMagnitudeEnd += channelsEnd[i].amp;
		}

		// Put the amps back into channel order
		qsort(channelsStart, dstBuffer.channelLayout.count, sizeof(struct channelMetadata), compareChannelMetadataChannel);
		qsort(channelsEnd, dstBuffer.channelLayout.count, sizeof(struct channelMetadata), compareChannelMetadataChannel);
	}

#if PRINT_CHANNEL_AMPS || PRINT_CHANNEL_DELAYS
	if (repeatCount == 0) {
		AZA_LOG_INFO("\n");
	}
#endif
	for (uint8_t c = 0; c < sideBuffer.channelLayout.count; c++) {
		float ampStart = srcAmpStart;
		float ampEnd = srcAmpEnd;
		if (dstBuffer.channelLayout.positions[c] != AZA_POS_SUBWOOFER) {
			ampStart *= (channelsStart[c].amp / totalMagnitudeStart) * (1.0f - minAmp) + minAmp;
			ampEnd *= (channelsEnd[c].amp / totalMagnitudeEnd) * (1.0f - minAmp) + minAmp;
		}
#if PRINT_CHANNEL_AMPS
		if (repeatCount == 0) {
			AZA_LOG_INFO("Channel %u amp: %f\n", (uint32_t)c, ampStart);
		}
#endif
#if PRINT_CHANNEL_DELAYS
		if (repeatCount == 0) {
			AZA_LOG_INFO("Channel %u delay: %f\n", (uint32_t)c, channelDelayStart[c]);
		}
#endif
		azaBufferMixFade(azaBufferOneChannel(sideBuffer, c), 1.0f, 1.0f, srcBuffer, ampStart, ampEnd);
	}
	if (data->config.mode == AZA_SPATIALIZE_ADVANCED) {
		// Gotta do the doppler
		azaDelayDynamic *delay = azaSpatializeGetDelayDynamic(data);
		err = azaEnsureChannels(&data->channelData, sideBuffer.channelLayout.count);
		if AZA_UNLIKELY(err) return err;
		err = azaEnsureChannels(&delay->channelData, sideBuffer.channelLayout.count);
		if AZA_UNLIKELY(err) return err;
		for (uint8_t c = 0; c < sideBuffer.channelLayout.count; c++) {
			azaDelayDynamicChannelConfig *channelConfig = azaDelayDynamicGetChannelConfig(delay, c);
			channelConfig->delay = channelDelayStart[c];
			azaSpatializeChannelData *channelData = azaGetChannelData(&data->channelData, c);
			channelData->filter.config.frequency = azaSpatializeGetFilterCutoff(channelDelayStart[c], channelDot[c]);
			// AZA_LOG_INFO("(c %u) filter freq = %f\n", c, channelData->filter.config.frequency);
			err = azaFilterProcess(&channelData->filter, azaBufferOneChannel(sideBuffer, c));
			if AZA_UNLIKELY(err) return err;
		}
		err = azaDelayDynamicProcess(delay, sideBuffer, channelDelayEnd);
		if AZA_UNLIKELY(err) return err;
	}
	azaBufferMix(dstBuffer, 1.0f, sideBuffer, 1.0f);
#if PRINT_CHANNEL_AMPS || PRINT_CHANNEL_DELAYS
	repeatCount = (repeatCount + 1) % 10;
#endif
	azaPopSideBuffer();
	return err;
}