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
	int err = azaKernelInit(kernel, 1+radius*2, 1+radius, resolution);
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

void azaResample(azaKernel *kernel, float factor, float *dst, int dstStride, int dstFrames, float *src, int srcStride, int srcFrameMin, int srcFrameMax, float srcSampleOffset) {
	const float rate = azaMinf(factor, 1.0f);
	for (uint32_t i = 0; i < (uint32_t)dstFrames; i++) {
		double pos = (double)i * (double)factor;
		int32_t frame = (int32_t)trunc(pos);
		float fraction = (float)(pos - (double)frame) + srcSampleOffset;
		// BIG TODO: Why is this just one channel???
		dst[i * dstStride] = azaSampleWithKernel1Ch(kernel, src, srcStride, srcFrameMin, srcFrameMax, false, frame, fraction, rate);
	}
}

void azaResampleAdd(azaKernel *kernel, float factor, float amp, float *dst, int dstStride, int dstFrames, float *src, int srcStride, int srcFrameMin, int srcFrameMax, float srcSampleOffset) {
	const float rate = azaMinf(factor, 1.0f);
	for (uint32_t i = 0; i < (uint32_t)dstFrames; i++) {
		double pos = (double)i * (double)factor;
		int32_t frame = (int32_t)trunc(pos);
		float fraction = (float)(pos - (double)frame) + srcSampleOffset;
		// BIG TODO: Why is this just one channel???
		dst[i * dstStride] += amp * azaSampleWithKernel1Ch(kernel, src, srcStride, srcFrameMin, srcFrameMax, false, frame, fraction, rate);
	}
}
