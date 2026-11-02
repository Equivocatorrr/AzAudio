/*
	File: azaKernel.c
	Author: Philip Haynes
*/

#include "azaKernel.h"

#include "../AzAudio.h"
#include "../error.h"



azaKernel azaKernelDefaultLanczos[AZA_KERNEL_DEFAULT_LANCZOS_COUNT] = {0};



int azaKernelInit(azaKernel *kernel, uint32_t length, uint32_t sampleZero, uint32_t scale) {
	kernel->length = length;
	kernel->sampleZero = sampleZero;
	kernel->scale = scale;
	kernel->size = length * scale;
	size_t tableLen;
	size_t allocSize = azaKernelGetDynAllocSize(length, scale, &tableLen);
	kernel->table = aza_calloc(allocSize, 1);
	if (!kernel->table) return AZA_ERROR_OUT_OF_MEMORY;
	kernel->packed = (float*)((char*)kernel->table + tableLen);
	return AZA_SUCCESS;
}

void azaKernelDeinit(azaKernel *kernel) {
	aza_free(kernel->table);
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
	int err = azaKernelInit(kernel, 1+radius*2, radius, resolution);
	if (err) return err;
	kernel->table[0] = 0.0f;
	for (uint32_t i = 0; i < radius * resolution; i++) {
		float value = azaLanczosf((float)i / (float)resolution, (float)radius);
		kernel->table[kernel->sampleZero*resolution - i] = value;
		kernel->table[kernel->sampleZero*resolution + i] = value;
	}
	kernel->table[kernel->size-1] = 0.0f;
	azaKernelPack(kernel);
	return AZA_SUCCESS;
}

void azaResample(azaKernel *kernel, float factor, float *dst, int dstChannels, int dstStride, int dstFrames, float *src, int srcStride, int srcFrameMin, int srcFrameMax, float srcSampleOffset) {
	const float rate = azaMinf(0.9f / factor, 1.0f);
	for (uint32_t i = 0; i < (uint32_t)dstFrames; i++) {
		double pos = (double)i * (double)factor + (double)srcSampleOffset;
		int32_t frame = (int32_t)trunc(pos);
		float fraction = (float)(pos - (double)frame);
		azaSampleWithKernel(&dst[i*dstStride], dstChannels, kernel, src, srcStride, srcFrameMin, srcFrameMax, false, frame, fraction, rate);
	}
}

void azaResampleAdd(azaKernel *kernel, float factor, float amp, float *dst, int dstChannels, int dstStride, int dstFrames, float *src, int srcStride, int srcFrameMin, int srcFrameMax, float srcSampleOffset) {
	const float rate = azaMinf(0.9f / factor, 1.0f);
	float toAdd[256];
	assert(dstChannels < 256);
	for (uint32_t i = 0; i < (uint32_t)dstFrames; i++) {
		double pos = (double)i * (double)factor + (double)srcSampleOffset;
		int32_t frame = (int32_t)trunc(pos);
		float fraction = (float)(pos - (double)frame);
		azaSampleWithKernel(toAdd, dstChannels, kernel, src, srcStride, srcFrameMin, srcFrameMax, false, frame, fraction, rate);
		for (int c = 0; c < dstChannels; c++) {
			dst[i*dstStride + c] += amp * toAdd[c];
		}
	}
}

uint32_t azaGetResampledDstFrameCount(uint32_t dstSamplerate, uint32_t srcSamplerate, uint32_t srcFrames) {
	return (uint32_t)floorf((float)srcFrames * (float)dstSamplerate / (float)srcSamplerate);
}

uint32_t azaGetResampledSrcFrameCount(uint32_t dstSamplerate, uint32_t srcSamplerate, uint32_t dstFrames) {
	return (uint32_t)ceilf((float)dstFrames * (float)srcSamplerate / (float)dstSamplerate);
}

void azaBufferResample(azaBuffer *dst, azaBuffer *src, azaKernel *kernel, float *srcFrameStart) {
	assert(src->samplerate);
	assert(dst->samplerate);
	float factor = (float)src->samplerate / (float)dst->samplerate;
	uint32_t srcFrameOffset = (uint32_t)floorf(*srcFrameStart);
	float srcSampleOffset = *srcFrameStart - (float)srcFrameOffset;
	azaResample(kernel, factor, dst->pSamples, dst->channelLayout.count, dst->stride, dst->frames, src->pSamples+srcFrameOffset*src->stride, src->stride, -(int)src->leadingFrames, src->frames+src->trailingFrames, srcSampleOffset);
	*srcFrameStart += (float)dst->frames * factor;
}