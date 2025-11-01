/*
	File: azaKernel.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_AZAKERNEL_H
#define AZAUDIO_AZAKERNEL_H

#include "../math.h"
#include "azaBuffer.h"

#ifdef __cplusplus
extern "C" {
#endif



typedef struct azaKernel {
	// length of the kernel (in samples)
	uint32_t length;
	// which sample along our length represents a time offset of zero
	uint32_t sampleZero;
	// How many sub-samples there are between each sample
	uint32_t scale;
	// total size of the useful part of table, which is length * scale. If padding exists from alignment, it is zeroed.
	uint32_t size;
	// Standard layout where kernel samples are in order. This is where you write the kernel before calling azaKernelPack.
	float *table;
	// An alternate layout of the table optimized for sampling (only works for sampling with rate=1.0f)
	// The following is a semantic representation of the layout:
	// struct {
	// 	float samples[length];
	// } subsamples[scale];
	float *packed;
} azaKernel;

// Given a kernel length and scale, returns how many bytes are needed to store the tables.
// If dstTableLen is not NULL, we provide the exact size in bytes of table (which is the offset to packed).
// Both table sizes are aligned on a 16-byte boundary.
static inline size_t azaKernelGetDynAllocSize(size_t length, size_t scale, size_t *dstTableLen) {
	size_t tableLen = aza_align(length * scale * sizeof(float), 16);
	if (dstTableLen) {
		*dstTableLen = tableLen;
	}
	size_t packedLen = aza_align(length * (scale + 1) * sizeof(float), 16);
	size_t result = tableLen + packedLen;
	return result;
}

// Creates a blank kernel
// Will allocate memory for the table (may return AZA_ERROR_OUT_OF_MEMORY)
int azaKernelInit(azaKernel *kernel, uint32_t length, uint32_t sampleZero, uint32_t scale);
void azaKernelDeinit(azaKernel *kernel);

// Must be called after kernel->table is populated, and before using the kernel for any sampling.
void azaKernelPack(azaKernel *kernel);

// Takes a single sample from the kernel itself
// pos is the location in the kernel in samples (not sub-samples)
float azaKernelSample(azaKernel *kernel, float pos);

// Uses the kernel to sample a single frame from the signal in src, where src[0] represents frame 0
// dst is an array of size dstChannels where the result will be placed
// srcStride is how many indices to skip for each sample in src (also the maximum channels that can be sampled at once)
// minFrame can be negative, and represents the lower bound of the useable range of src (inclusive, src[minFrame*srcStride] must be valid)
// maxFrame represents the upper bound of the useable range of src (exclusive, src[(maxFrame-1)*srcStride] must be valid)
// wrap determines whether the range wraps around, eg. such that a frame towards the end of the range may use samples at the beginning and vice-versa.
// frame is the sampling location in terms of frames.
// fraction is the fractional part of the sampling location in the range 0.0f to 1.0f
// rate determines how quickly we traverse the kernel in the range 0.0f to 1.0f. This is used, for example, for low-passing with a lanczos kernel below the source signal's nyquist frequency, such as to avoid aliasing in the destination. Note that the computational cost of this function scales by 1/rate, as more total samples will be needed to traverse the whole kernel when rate is lower. It's recommended to either use smaller kernels for lower rates, or perhaps even downsample the source first, if applicable.
void azaSampleWithKernel(float *dst, int dstChannels, azaKernel *kernel, float *src, int srcStride, int minFrame, int maxFrame, bool wrap, int32_t frame, float fraction, float rate);

// 1-channel version of the above to be more like the old interface.
static inline float azaSampleWithKernel1Ch(azaKernel *kernel, float *src, int srcStride, int minFrame, int maxFrame, bool wrap, int32_t frame, float fraction, float rate) {
	float result;
	azaSampleWithKernel(&result, 1, kernel, src, srcStride, minFrame, maxFrame, wrap, frame, fraction, rate);
	return result;
}

static inline void azaBufferSampleWithKernel(float *dst, int dstChannels, azaKernel *kernel, azaBuffer *src, bool wrap, uint32_t frame, float fraction, float rate) {
	int minFrame = -(int)src->leadingFrames;
	int maxFrame = (int)(src->frames + src->trailingFrames);
	azaSampleWithKernel(dst, dstChannels, kernel, src->pSamples, (int)src->stride, minFrame, maxFrame, wrap, frame, fraction, rate);
}

// How many total kernel samples have been taken as scalars (to measure SIMD efficacy)
extern uint64_t azaKernelScalarSamples;
// How many total kernel samples have been taken as vectors (to measure SIMD efficacy)
extern uint64_t azaKernelVectorSamples;

// Maximum radius
enum { AZA_KERNEL_DEFAULT_LANCZOS_COUNT = 128 };
// Lanczos kernels indexed by radius-1
extern azaKernel azaKernelDefaultLanczos[AZA_KERNEL_DEFAULT_LANCZOS_COUNT];

// asserts radius is between 1 and AZA_KERNEL_DEFAULT_LANCZOS_COUNT inclusive
static inline azaKernel* azaKernelGetDefaultLanczos(uint32_t radius) {
	assert(radius >= 1);
	assert(radius <= AZA_KERNEL_DEFAULT_LANCZOS_COUNT);
	return &azaKernelDefaultLanczos[radius-1];
}

static inline uint32_t azaKernelGetRadiusForRate(float rate, uint32_t maxRadius) {
	uint32_t radius = AZA_CLAMP((uint32_t)floorf(rate*(float)maxRadius), 1, maxRadius);
	return radius;
}

// Makes a lanczos kernel. resolution is the number of samples between zero crossings
// May return AZA_ERROR_OUT_OF_MEMORY
int azaKernelMakeLanczos(azaKernel *kernel, uint32_t resolution, uint32_t radius);

// Performs resampling of src into dst with the given scaling factor and kernel.
// srcFrames is not actually needed here because the sampleable extent is provided by srcFrameMin and srcFrameMax, but for this description it refers to how many samples within src are considered the "meat" of the signal (excluding padding carried over from the last iteration of resampling a stream).
// factor is the scaling ratio (defined roughly as `srcFrames / dstFrames`), passed in explicitly because the exact desired ratio may not be represented accurately by a ratio of the length of two small buffers. For no actual time scaling, this ratio should be perfectly represented by `srcSamplerate / dstSamplerate`.
// src should point at least `-srcFrameMin` frames into an existing source buffer with a total extent of `srcFrameMax-srcFrameMin`.
// srcFrameMin and srcFrameMax allow the accessible extent of src to go outside of the given 0...srcFrames extent, since that's required for perfect resampling of chunks of a stream (while accepting some latency). Ideally, srcFrameMin would be `-kernel->size` and srcFrameMax would be `srcFrames+kernel->size` for a symmetric kernel. For a non-symmetric kernel, srcFrameMin can be 0, and srcFrameMax would still be srcFrames+kernel->size. For two isolated buffers, srcFrameMin should be 0 and srcFrameMax should be srcFrames. Any samples outside of this extent will be considered to be zeroes.
// srcSampleOffset should be in the range 0 to 1
void azaResample(azaKernel *kernel, float factor, float *dst, int dstStride, int dstFrames, float *src, int srcStride, int srcFrameMin, int srcFrameMax, float srcSampleOffset);

// Same as azaResample, except the resampled values are added to dst instead of replacing them. Every sample is multiplied by amp before being added.
void azaResampleAdd(azaKernel *kernel, float factor, float amp, float *dst, int dstStride, int dstFrames, float *src, int srcStride, int srcFrameMin, int srcFrameMax, float srcSampleOffset);



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZAKERNEL_H
