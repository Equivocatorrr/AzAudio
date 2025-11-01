/*
	File: azaKernel.c
	Author: Philip Haynes
	Specialized implementations of azaKernel procedures and dispatch.
	Non-specialized code still lives in dsp.c
	Implements the following (declared in dsp.h):
		- azaKernelSample(2)
		- azaSampleWithKernel(11)
*/

#include "../dsp/azaKernel.h"
#include "../simd.h"
#include "../AzAudio.h"

uint64_t azaKernelScalarSamples = 0;
uint64_t azaKernelVectorSamples = 0;

// NOTE: There are still opportunities for optimizing this for special cases, such as smartly handling srcStride of 1. As for how much difference those changes would make remains to be seen.

// NOTE: We may decide to remove the packed kernel layout entirely since according to my testing it's only marginally faster than the sparse layout code, at least on a Ryzen 9 5900x. I suspect this would be less close on older or cheaper CPUs, so I'm leaving it as-is for now.
#define USE_PACKED_KERNEL 1

float azaKernelSample_rate1(azaKernel *kernel, float pos) {
	float actualPos = pos + (float)kernel->sampleZero;
	if (actualPos < 0.0f) return 0.0f;
	int32_t index = (int32_t)actualPos;
	if ((uint32_t)index+1 > kernel->length) return 0.0f;
	actualPos -= (float)index;
	actualPos *= kernel->scale;
	int32_t subsample = (int32_t)actualPos;
	actualPos -= (float)subsample;
	assert(subsample < (int32_t)kernel->scale);
	float srcSubsample0 = *(kernel->packed + ((subsample+0) * kernel->length) + index);
	float srcSubsample1 = *(kernel->packed + ((subsample+1) * kernel->length) + index);
	float result = azaLerpf(srcSubsample0, srcSubsample1, actualPos);
	azaKernelScalarSamples++;
	return result;
}

float azaKernelSample(azaKernel *kernel, float pos) {
	float actualPos = (pos + (float)kernel->sampleZero) * (float)kernel->scale;
	if (actualPos < 0.0f) return 0.0f;
	int32_t index = (int32_t)actualPos;
	if ((uint32_t)index+1 >= kernel->size) return 0.0f;
	float t = actualPos - (float)index;
	assert(t >= 0.0f);
	float result = azaLerpf(kernel->table[index], kernel->table[index+1], t);
	azaKernelScalarSamples++;
	return result;
}

// While rate isn't needed here, by doing the scalar ones in the packed layout we improve cache coherency, especially when handling tails in the SIMD versions
AZA_SIMD_FEATURES("sse,fma")
static float azaKernelSample_sse_fma_rate1(azaKernel *kernel, float pos) {
	float actualPos = pos + (float)kernel->sampleZero;
	if (actualPos < 0.0f) return 0.0f;
	int32_t index = (int32_t)actualPos;
	if ((uint32_t)index+1 > kernel->length) return 0.0f;
	actualPos -= (float)index;
	actualPos *= kernel->scale;
	int32_t subsample = (int32_t)actualPos;
	actualPos -= (float)subsample;
	assert(subsample < (int32_t)kernel->scale);
	float srcSubsample0 = *(kernel->packed + ((subsample+0) * kernel->length) + index);
	float srcSubsample1 = *(kernel->packed + ((subsample+1) * kernel->length) + index);
	float result = azaLerpf_sse_fma(srcSubsample0, srcSubsample1, actualPos);
	azaKernelScalarSamples++;
	return result;
}

AZA_SIMD_FEATURES("sse,fma")
static float azaKernelSample_sse_fma(azaKernel *kernel, float pos) {
	float actualPos = (pos + (float)kernel->sampleZero) * (float)kernel->scale;
	if (actualPos < 0.0f) return 0.0f;
	int32_t index = (int32_t)actualPos;
	if ((uint32_t)index+1 >= kernel->size) return 0.0f;
	float t = actualPos - (float)index;
	assert(t >= 0.0f);
	float result = azaLerpf_sse_fma(kernel->table[index], kernel->table[index+1], t);
	azaKernelScalarSamples++;
	return result;
}

AZA_SIMD_FEATURES("sse")
static __m128 azaKernelSample_x4_sse_rate1(azaKernel *kernel, float pos) {
	azaKernelVectorSamples += 4;
	// We get to do the easy thing and use the packed kernel layout
	float actualPos = pos + (float)kernel->sampleZero;
	assert(actualPos >= 0.0f);
	int32_t index = (int32_t)actualPos;
	int32_t subsample = (int32_t)(actualPos * (float)kernel->scale) - index * kernel->scale;
	// We won't be doing any masking, so don't be calling this if you don't handle tails as scalars!
	assert(index <= (int32_t)kernel->length-4);
	assert(index >= 0);
	actualPos -= (float)index;
	actualPos *= kernel->scale;
	// int32_t subsample = (int32_t)actualPos;
	actualPos -= (float)subsample;
	assert(subsample < (int32_t)kernel->scale);
	float *srcSubsample0 = kernel->packed + ((subsample+0) * kernel->length);
	__m128 samples0 = _mm_loadu_ps(srcSubsample0 + index);
	float *srcSubsample1 = kernel->packed + ((subsample+1) * kernel->length);
	__m128 samples1 = _mm_loadu_ps(srcSubsample1 + index);
	__m128 result = azaLerp_x4_sse(samples0, samples1, _mm_set1_ps(actualPos));
	return result;
}

AZA_SIMD_FEATURES("sse")
static __m128 azaKernelSample_x4_sse(azaKernel *kernel, float pos, float rate) {
	azaKernelVectorSamples += 4;
	// We have to sample according to our rate
	__m128 offset_x4 = _mm_setr_ps(0.0f, 1.0f, 2.0f, 3.0f);
	__m128 rate_x4 = _mm_set1_ps(rate * (float)kernel->scale);
	float actualPos = (pos + (float)kernel->sampleZero) * (float)kernel->scale;
	assert(actualPos >= 0.0f);
	__m128 actualPos_x4 = _mm_add_ps(_mm_set1_ps(actualPos), _mm_mul_ps(rate_x4, offset_x4));
	alignas(16) float fIndices[4];
	_mm_store_ps(fIndices, actualPos_x4);
	int indices[4] = {
		(int)fIndices[0],
		(int)fIndices[1],
		(int)fIndices[2],
		(int)fIndices[3],
	};
	fIndices[0] = (float)indices[0];
	fIndices[1] = (float)indices[1];
	fIndices[2] = (float)indices[2];
	fIndices[3] = (float)indices[3];
	__m128 t_x4 = _mm_sub_ps(actualPos_x4, _mm_loadu_ps(fIndices));
	__m128 samples0 = _mm_setr_ps(kernel->table[indices[0]], kernel->table[indices[1]], kernel->table[indices[2]], kernel->table[indices[3]]);
	__m128 samples1 = _mm_setr_ps(kernel->table[indices[0]+1], kernel->table[indices[1]+1], kernel->table[indices[2]+1], kernel->table[indices[3]+1]);
	__m128 result = azaLerp_x4_sse(samples0, samples1, t_x4);
	return result;
}

AZA_SIMD_FEATURES("sse2")
static inline __m128 azaKernelSample_x4_sse2_rate1(azaKernel *kernel, float pos) {
	return azaKernelSample_x4_sse_rate1(kernel, pos);
}

AZA_SIMD_FEATURES("sse2")
static __m128 azaKernelSample_x4_sse2(azaKernel *kernel, float pos, float rate) {
	azaKernelVectorSamples += 4;
	// We have to sample according to our rate
	__m128 offset_x4 = _mm_setr_ps(0.0f, 1.0f, 2.0f, 3.0f);
	__m128 rate_x4 = _mm_set1_ps(rate * (float)kernel->scale);
	float actualPos = (pos + (float)kernel->sampleZero) * (float)kernel->scale;
	assert(actualPos >= 0.0f);
	__m128 actualPos_x4 = _mm_add_ps(_mm_set1_ps(actualPos), _mm_mul_ps(rate_x4, offset_x4));
	// sse2 gets us cvtps2dq and cvtdq2ps
	__m128i index_x4 = _mm_cvtps_epi32(actualPos_x4);
	alignas(16) int32_t indices[4];
	_mm_store_si128((__m128i*)indices, index_x4);
	__m128 t_x4 = _mm_sub_ps(actualPos_x4, _mm_cvtepi32_ps(index_x4));
	__m128 samples0 = _mm_setr_ps(kernel->table[indices[0]], kernel->table[indices[1]], kernel->table[indices[2]], kernel->table[indices[3]]);
	__m128 samples1 = _mm_setr_ps(kernel->table[indices[0]+1], kernel->table[indices[1]+1], kernel->table[indices[2]+1], kernel->table[indices[3]+1]);
	__m128 result = azaLerp_x4_sse(samples0, samples1, t_x4);
	return result;
}

AZA_SIMD_FEATURES("avx")
static __m256 azaKernelSample_x8_avx_rate1(azaKernel *kernel, float pos) {
	azaKernelVectorSamples += 8;
	// We get to do the easy thing and use the packed kernel layout
	float actualPos = pos + (float)kernel->sampleZero;
	assert(actualPos >= 0.0f);
	int32_t index = (int32_t)actualPos;
	// We won't be doing any masking, so don't be calling this if you don't handle tails as scalars!
	assert(index <= (int32_t)kernel->length-8);
	assert(index >= 0);
	actualPos -= (float)index;
	actualPos *= kernel->scale;
	int32_t subsample = (int32_t)actualPos;
	actualPos -= (float)subsample;
	assert(subsample < (int32_t)kernel->scale);
	float *srcSubsample0 = kernel->packed + ((subsample+0) * kernel->length);
	float *srcSubsample1 = kernel->packed + ((subsample+1) * kernel->length);
	__m256 samples0 = _mm256_loadu_ps(srcSubsample0 + index);
	__m256 samples1 = _mm256_loadu_ps(srcSubsample1 + index);
	__m256 result = azaLerp_x8_avx(samples0, samples1, _mm256_set1_ps(actualPos));
	return result;
}

AZA_SIMD_FEATURES("avx")
static __m256 azaKernelSample_x8_avx(azaKernel *kernel, float pos, float rate) {
	azaKernelVectorSamples += 8;
	// We have to sample according to our rate
#if 1 // Do the normal 256-bit wide thing

	__m256 offset_x8 = _mm256_setr_ps(0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f);
	__m256 rate_x8 = _mm256_set1_ps(rate * (float)kernel->scale);
	float actualPos = (pos + (float)kernel->sampleZero) * (float)kernel->scale;
	assert(actualPos >= 0.0f);
	// By deliberately not using an fmadd here, we open the CPU up to pipeline the mul alongside the computation of actualPos
	__m256 actualPos_x8 = _mm256_add_ps(_mm256_mul_ps(rate_x8, offset_x8), _mm256_set1_ps(actualPos));
	__m256i index_x8 = _mm256_cvtps_epi32(actualPos_x8);
	alignas(32) int32_t indices[8];
	_mm256_store_si256((__m256i*)indices, index_x8);
	__m256 t_x8 = _mm256_sub_ps(actualPos_x8, _mm256_cvtepi32_ps(index_x8));
#if 0
	alignas(32) float values0[8];
	for (int s = 0; s < 8; s++) {
		values0[s] = kernel->table[indices[s]];
	}
	__m256 samples0 = _mm256_load_ps(values0);
	alignas(32) float values1[8];
	for (int s = 0; s < 8; s++) {
		values1[s] = kernel->table[indices[s]+1];
	}
	__m256 samples1 = _mm256_load_ps(values1);
#else
	__m256 samples0 = _mm256_setr_ps(kernel->table[indices[0]], kernel->table[indices[1]], kernel->table[indices[2]], kernel->table[indices[3]], kernel->table[indices[4]], kernel->table[indices[5]], kernel->table[indices[6]], kernel->table[indices[7]]);
	__m256 samples1 = _mm256_setr_ps(kernel->table[indices[0]+1], kernel->table[indices[1]+1], kernel->table[indices[2]+1], kernel->table[indices[3]+1], kernel->table[indices[4]+1], kernel->table[indices[5]+1], kernel->table[indices[6]+1], kernel->table[indices[7]+1]);
#endif
	__m256 result = azaLerp_x8_avx(samples0, samples1, t_x8);
	return result;

#else // Spread work across registers to improve pipelining, if that actually helps (this did appear to help for a version of this function that checked rate internally. Now that it's being inlined, the wider version seems better, probably due to some loop unrolling magic :)

	__m128 offset_x4[2] = {
		_mm_setr_ps(0.0f, 1.0f, 2.0f, 3.0f),
		_mm_setr_ps(4.0f, 5.0f, 6.0f, 7.0f),
	};
	__m128 rate_x4 = _mm_set1_ps(rate * (float)kernel->scale);
	float actualPos = (pos + (float)kernel->sampleZero) * (float)kernel->scale;
	assert(actualPos >= 0.0f);
	__m128 actualPos_x4[2] = {
		_mm_add_ps(_mm_mul_ps(rate_x4, offset_x4[0]), _mm_set1_ps(actualPos)),
		_mm_add_ps(_mm_mul_ps(rate_x4, offset_x4[1]), _mm_set1_ps(actualPos)),
	};
	__m128i index_x4[2] = {
		_mm_cvtps_epi32(actualPos_x4[0]),
		_mm_cvtps_epi32(actualPos_x4[1]),
	};
	__m128 t_x4[2] = {
		_mm_sub_ps(actualPos_x4[0], _mm_cvtepi32_ps(index_x4[0])),
		_mm_sub_ps(actualPos_x4[1], _mm_cvtepi32_ps(index_x4[1])),
	};
	alignas(16) int32_t indices[8];
	_mm_store_si128((__m128i*)&indices[0], index_x4[0]);
	_mm_store_si128((__m128i*)&indices[4], index_x4[1]);
	__m128 samples0[2] = {
		_mm_setr_ps(kernel->table[indices[0]], kernel->table[indices[1]], kernel->table[indices[2]], kernel->table[indices[3]]),
		_mm_setr_ps(kernel->table[indices[4]], kernel->table[indices[5]], kernel->table[indices[6]], kernel->table[indices[7]]),
	};
	__m128 samples1[2] = {
		_mm_setr_ps(kernel->table[indices[0]+1], kernel->table[indices[1]+1], kernel->table[indices[2]+1], kernel->table[indices[3]+1]),
		_mm_setr_ps(kernel->table[indices[4]+1], kernel->table[indices[5]+1], kernel->table[indices[6]+1], kernel->table[indices[7]+1]),
	};
	__m128 result[2] = {
		azaLerp_x4_sse(samples0[0], samples1[0], t_x4[0]),
		azaLerp_x4_sse(samples0[1], samples1[1], t_x4[1]),
	};
	__m256 finalResult = _mm256_insertf128_ps(_mm256_castps128_ps256(result[0]), result[1], 1);
	return finalResult;
#endif
}

AZA_SIMD_FEATURES("avx,fma")
static __m256 azaKernelSample_x8_avx_fma_rate1(azaKernel *kernel, float pos) {
	azaKernelVectorSamples += 8;
	// We get to do the easy thing and use the packed kernel layout
	float actualPos = pos + (float)kernel->sampleZero;
	assert(actualPos >= 0.0f);
	int32_t index = (int32_t)actualPos;
	// We won't be doing any masking, so don't be calling this if you don't handle tails as scalars!
	assert(index <= (int32_t)kernel->length-8);
	assert(index >= 0);
	actualPos -= (float)index;
	actualPos *= kernel->scale;
	int32_t subsample = (int32_t)actualPos;
	actualPos -= (float)subsample;
	assert(subsample < (int32_t)kernel->scale);
	float *srcSubsample0 = kernel->packed + ((subsample+0) * kernel->length);
	float *srcSubsample1 = kernel->packed + ((subsample+1) * kernel->length);
	__m256 samples0 = _mm256_loadu_ps(srcSubsample0 + index);
	__m256 samples1 = _mm256_loadu_ps(srcSubsample1 + index);
	__m256 result = azaLerp_x8_avx_fma(samples0, samples1, _mm256_set1_ps(actualPos));
	return result;
}

AZA_SIMD_FEATURES("avx,fma")
static __m256 azaKernelSample_x8_avx_fma(azaKernel *kernel, float pos, float rate) {
	azaKernelVectorSamples += 8;
	// We have to sample according to our rate
#if 1
	__m256 offset_x8 = _mm256_setr_ps(0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f);
	__m256 rate_x8 = _mm256_set1_ps(rate * (float)kernel->scale);
	float actualPos = (pos + (float)kernel->sampleZero) * (float)kernel->scale;
	assert(actualPos >= 0.0f);
	// By deliberately not using an fmadd here, we open the CPU up to pipeline the mul alongside the computation of actualPos
	__m256 actualPos_x8 = _mm256_fmadd_ps(rate_x8, offset_x8, _mm256_set1_ps(actualPos));
	// __m256 actualPos_x8 = _mm256_add_ps(_mm256_mul_ps(rate_x8, offset_x8), _mm256_set1_ps(actualPos));
	__m256i index_x8 = _mm256_cvtps_epi32(actualPos_x8);
	alignas(32) int32_t indices[8];
	_mm256_store_si256((__m256i*)indices, index_x8);
	__m256 t_x8 = _mm256_sub_ps(actualPos_x8, _mm256_cvtepi32_ps(index_x8));
#if 0
	alignas(32) float values0[8];
	for (int s = 0; s < 8; s++) {
		values0[s] = kernel->table[indices[s]];
	}
	__m256 samples0 = _mm256_load_ps(values0);
	alignas(32) float values1[8];
	for (int s = 0; s < 8; s++) {
		values1[s] = kernel->table[indices[s]+1];
	}
	__m256 samples1 = _mm256_load_ps(values1);
#else
	__m256 samples0 = _mm256_setr_ps(kernel->table[indices[0]], kernel->table[indices[1]], kernel->table[indices[2]], kernel->table[indices[3]], kernel->table[indices[4]], kernel->table[indices[5]], kernel->table[indices[6]], kernel->table[indices[7]]);
	__m256 samples1 = _mm256_setr_ps(kernel->table[indices[0]+1], kernel->table[indices[1]+1], kernel->table[indices[2]+1], kernel->table[indices[3]+1], kernel->table[indices[4]+1], kernel->table[indices[5]+1], kernel->table[indices[6]+1], kernel->table[indices[7]+1]);
#endif
	__m256 result = azaLerp_x8_avx(samples0, samples1, t_x8);
	return result;
#else
	__m128 offset_x4[2] = {
		_mm_setr_ps(0.0f, 1.0f, 2.0f, 3.0f),
		_mm_setr_ps(4.0f, 5.0f, 6.0f, 7.0f),
	};
	__m128 rate_x4 = _mm_set1_ps(rate * (float)kernel->scale);
	float actualPos = (pos + (float)kernel->sampleZero) * (float)kernel->scale;
	assert(actualPos >= 0.0f);
	__m128 actualPos_x4[2] = {
		_mm_fmadd_ps(rate_x4, offset_x4[0], _mm_set1_ps(actualPos)),
		_mm_fmadd_ps(rate_x4, offset_x4[1], _mm_set1_ps(actualPos)),
	};
	__m128i index_x4[2] = {
		_mm_cvtps_epi32(actualPos_x4[0]),
		_mm_cvtps_epi32(actualPos_x4[1]),
	};
	__m128 t_x4[2] = {
		_mm_sub_ps(actualPos_x4[0], _mm_cvtepi32_ps(index_x4[0])),
		_mm_sub_ps(actualPos_x4[1], _mm_cvtepi32_ps(index_x4[1])),
	};
	alignas(16) int32_t indices[8];
	_mm_store_si128((__m128i*)&indices[0], index_x4[0]);
	_mm_store_si128((__m128i*)&indices[4], index_x4[1]);
	__m128 samples0[2] = {
		_mm_setr_ps(kernel->table[indices[0]], kernel->table[indices[1]], kernel->table[indices[2]], kernel->table[indices[3]]),
		_mm_setr_ps(kernel->table[indices[4]], kernel->table[indices[5]], kernel->table[indices[6]], kernel->table[indices[7]]),
	};
	__m128 samples1[2] = {
		_mm_setr_ps(kernel->table[indices[0]+1], kernel->table[indices[1]+1], kernel->table[indices[2]+1], kernel->table[indices[3]+1]),
		_mm_setr_ps(kernel->table[indices[4]+1], kernel->table[indices[5]+1], kernel->table[indices[6]+1], kernel->table[indices[7]+1]),
	};
	__m128 result[2] = {
		azaLerp_x4_sse_fma(samples0[0], samples1[0], t_x4[0]),
		azaLerp_x4_sse_fma(samples0[1], samples1[1], t_x4[1]),
	};
	__m256 finalResult = _mm256_insertf128_ps(_mm256_castps128_ps256(result[0]), result[1], 1);
	return finalResult;
#endif
}

AZA_SIMD_FEATURES("avx2,fma")
static inline __m256 azaKernelSample_x8_avx2_fma_rate1(azaKernel *kernel, float pos) {
	return azaKernelSample_x8_avx_fma_rate1(kernel, pos);
}

AZA_SIMD_FEATURES("avx2,fma")
static __m256 azaKernelSample_x8_avx2_fma(azaKernel *kernel, float pos, float rate) {
	azaKernelVectorSamples += 8;
	// We have to sample according to our rate
	__m256 offset_x8 = _mm256_setr_ps(0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f);
	__m256 rate_x8 = _mm256_set1_ps(rate * (float)kernel->scale);
	float actualPos = (pos + (float)kernel->sampleZero) * (float)kernel->scale;
	assert(actualPos >= 0.0f);
	// By deliberately not using an fmadd here, we open the CPU up to pipeline the mul alongside the computation of actualPos
	__m256 actualPos_x8 = _mm256_add_ps(_mm256_mul_ps(rate_x8, offset_x8), _mm256_set1_ps(actualPos));
	__m256i index_x8 = _mm256_cvtps_epi32(actualPos_x8);
	__m256 t_x8 = _mm256_sub_ps(actualPos_x8, _mm256_cvtepi32_ps(index_x8));
	__m256 samples0 = _mm256_i32gather_ps(kernel->table, index_x8, sizeof(float));
	index_x8 = _mm256_add_epi32(index_x8, _mm256_set1_epi32(1));
	__m256 samples1 = _mm256_i32gather_ps(kernel->table, index_x8, sizeof(float));
	__m256 result = azaLerp_x8_avx(samples0, samples1, t_x8);
	return result;
}

void azaSampleWithKernel_scalar(float *dst, int dstChannels, azaKernel *kernel, float *src, int srcStride, int minFrame, int maxFrame, bool wrap, int32_t frame, float fraction, float rate) {
	memset(dst, 0, sizeof(*dst)*dstChannels);
	float kernelIntegral = 0.0f;
	int srcStart = frame + (int)ceilf(-(float)kernel->sampleZero / rate);
	int srcLen = (int)ceilf((float)kernel->length / rate) - 1;
	int srcEnd = srcStart + srcLen;
	float kernelPos = rate * (1.0f-fraction) - (float)kernel->sampleZero;
	if (wrap) {
		for (int srcIndex = srcStart; srcIndex < srcEnd; srcIndex++) {
			int i = azaWrapi2(srcIndex, minFrame, maxFrame);
			float k = azaKernelSample(kernel, kernelPos);
			kernelIntegral += k;
			kernelPos += rate;
			for (int c = 0; c < dstChannels; c++) {
				float s = src[i * srcStride + c];
				dst[c] += s * k;
			}
		}
	} else {
		int srcStartActual = AZA_CLAMP(srcStart, minFrame, maxFrame-1);
		kernelPos += (float)(srcStartActual - srcStart) * rate;
		int srcEndActual = AZA_CLAMP(srcEnd, minFrame+1, maxFrame);
		for (int srcIndex = srcStartActual; srcIndex < srcEndActual; srcIndex++) {
			int i = srcIndex;
			float k = azaKernelSample(kernel, kernelPos);
			kernelIntegral += k;
			kernelPos += rate;
			for (int c = 0; c < dstChannels; c++) {
				float s = src[i * srcStride + c];
				dst[c] += s * k;
			}
		}
	}
	if (kernelIntegral > 0.0f) {
		for (int c = 0; c < dstChannels; c++) {
			dst[c] /= kernelIntegral;
		}
	}
}

AZA_SIMD_FEATURES("sse")
void azaSampleWithKernel_sse(float *dst, int dstChannels, azaKernel *kernel, float *src, int srcStride, int minFrame, int maxFrame, bool wrap, int32_t frame, float fraction, float rate) {
	memset(dst, 0, sizeof(*dst)*dstChannels);
	__m128 kernelIntegral_x4 = _mm_setzero_ps();
	float kernelIntegral = 0.0f;
	int srcStart = frame + (int)ceilf(-(float)kernel->sampleZero / rate);
	int srcLen = (int)ceilf((float)kernel->length / rate) - 1;
	int srcEnd = srcStart + srcLen;
	float kernelPos = rate * (1.0f-fraction) - (float)kernel->sampleZero;
	if (!wrap) {
		int srcStartActual = AZA_CLAMP(srcStart, minFrame, maxFrame-1);
		kernelPos += (float)(srcStartActual - srcStart) * rate;
		srcStart = srcStartActual;
		srcEnd = AZA_CLAMP(srcEnd, minFrame+1, maxFrame);
	}
	int srcIndex = srcStart;
	int endSSE = AZA_MIN(srcEnd, maxFrame);
#if USE_PACKED_KERNEL
	if (rate == 1.0f) {
		if (wrap) {
			for (; srcIndex < minFrame; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
		for (; srcIndex <= endSSE-4; srcIndex+=4) {
			__m128 kernelSamples = azaKernelSample_x4_sse_rate1(kernel, kernelPos);
			kernelIntegral_x4 = _mm_add_ps(kernelIntegral_x4, kernelSamples);
			kernelPos += 4.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *pSample = src + srcIndex * srcStride + c;
				__m128 srcSamples = _mm_setr_ps(*pSample, *(pSample + srcStride), *(pSample + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride));
				dst[c] += aza_mm_hsum_ps_sse(_mm_mul_ps(kernelSamples, srcSamples));
			}
		}
		if (wrap) {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		} else {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = srcIndex;
				float k = azaKernelSample_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
	} else {
#endif
		if (wrap) {
			for (; srcIndex < minFrame; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
		for (; srcIndex <= endSSE-4; srcIndex+=4) {
			__m128 kernelSamples = azaKernelSample_x4_sse(kernel, kernelPos, rate);
			kernelIntegral_x4 = _mm_add_ps(kernelIntegral_x4, kernelSamples);
			kernelPos += rate*4.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *pSample = src + srcIndex * srcStride + c;
				__m128 srcSamples = _mm_setr_ps(*pSample, *(pSample + srcStride), *(pSample + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride));
				dst[c] += aza_mm_hsum_ps_sse(_mm_mul_ps(kernelSamples, srcSamples));
			}
		}
		if (wrap) {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		} else {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = srcIndex;
				float k = azaKernelSample(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
#if USE_PACKED_KERNEL
	}
#endif
	kernelIntegral += aza_mm_hsum_ps_sse(kernelIntegral_x4);
	if (kernelIntegral > 0.0f) {
		for (int c = 0; c < dstChannels; c++) {
			dst[c] /= kernelIntegral;
		}
	}
}

AZA_SIMD_FEATURES("sse2")
void azaSampleWithKernel_sse2(float *dst, int dstChannels, azaKernel *kernel, float *src, int srcStride, int minFrame, int maxFrame, bool wrap, int32_t frame, float fraction, float rate) {
	memset(dst, 0, sizeof(*dst)*dstChannels);
	__m128 kernelIntegral_x4 = _mm_setzero_ps();
	float kernelIntegral = 0.0f;
	int srcStart = frame + (int)ceilf(-(float)kernel->sampleZero / rate);
	int srcLen = (int)ceilf((float)kernel->length / rate) - 1;
	int srcEnd = srcStart + srcLen;
	float kernelPos = rate * (1.0f-fraction) - (float)kernel->sampleZero;
	if (!wrap) {
		int srcStartActual = AZA_CLAMP(srcStart, minFrame, maxFrame-1);
		kernelPos += (float)(srcStartActual - srcStart) * rate;
		srcStart = srcStartActual;
		srcEnd = AZA_CLAMP(srcEnd, minFrame+1, maxFrame);
	}
	int srcIndex = srcStart;
	int endSSE = AZA_MIN(srcEnd, maxFrame);
#if USE_PACKED_KERNEL
	if (rate == 1.0f) {
		if (wrap) {
			for (; srcIndex < minFrame; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
		for (; srcIndex <= endSSE-4; srcIndex+=4) {
			__m128 kernelSamples = azaKernelSample_x4_sse2_rate1(kernel, kernelPos); // This is the only difference from the sse version.
			kernelIntegral_x4 = _mm_add_ps(kernelIntegral_x4, kernelSamples);
			kernelPos += 4.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *pSample = src + srcIndex * srcStride + c;
				__m128 srcSamples = _mm_setr_ps(*pSample, *(pSample + srcStride), *(pSample + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride));
				dst[c] += aza_mm_hsum_ps_sse(_mm_mul_ps(kernelSamples, srcSamples));
			}
		}
		if (wrap) {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		} else {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = srcIndex;
				float k = azaKernelSample_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
	} else {
#endif
		if (wrap) {
			for (; srcIndex < minFrame; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
		for (; srcIndex <= endSSE-4; srcIndex+=4) {
			__m128 kernelSamples = azaKernelSample_x4_sse2(kernel, kernelPos, rate); // This is the only difference from the sse version.
			kernelIntegral_x4 = _mm_add_ps(kernelIntegral_x4, kernelSamples);
			kernelPos += rate*4.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *pSample = src + srcIndex * srcStride + c;
				__m128 srcSamples = _mm_setr_ps(*pSample, *(pSample + srcStride), *(pSample + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride));
				dst[c] += aza_mm_hsum_ps_sse(_mm_mul_ps(kernelSamples, srcSamples));
			}
		}
		if (wrap) {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		} else {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = srcIndex;
				float k = azaKernelSample(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
#if USE_PACKED_KERNEL
	}
#endif
	kernelIntegral += aza_mm_hsum_ps_sse(kernelIntegral_x4);
	if (kernelIntegral > 0.0f) {
		for (int c = 0; c < dstChannels; c++) {
			dst[c] /= kernelIntegral;
		}
	}
}

// The only benefit of this one over sse2 is the hsum, which doesn't have a measurable effect, probably delete this later
AZA_SIMD_FEATURES("sse3")
void azaSampleWithKernel_sse3(float *dst, int dstChannels, azaKernel *kernel, float *src, int srcStride, int minFrame, int maxFrame, bool wrap, int32_t frame, float fraction, float rate) {
	memset(dst, 0, sizeof(*dst)*dstChannels);
	__m128 kernelIntegral_x4 = _mm_setzero_ps();
	float kernelIntegral = 0.0f;
	int srcStart = frame + (int)ceilf(-(float)kernel->sampleZero / rate);
	int srcLen = (int)ceilf((float)kernel->length / rate) - 1;
	int srcEnd = srcStart + srcLen;
	float kernelPos = rate * (1.0f-fraction) - (float)kernel->sampleZero;
	if (!wrap) {
		int srcStartActual = AZA_CLAMP(srcStart, minFrame, maxFrame-1);
		kernelPos += (float)(srcStartActual - srcStart) * rate;
		srcStart = srcStartActual;
		srcEnd = AZA_CLAMP(srcEnd, minFrame+1, maxFrame);
	}
	int srcIndex = srcStart;
	int endSSE = AZA_MIN(srcEnd, maxFrame);
#if USE_PACKED_KERNEL
	if (rate == 1.0f) {
		if (wrap) {
			for (; srcIndex < minFrame; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
		for (; srcIndex <= endSSE-4; srcIndex+=4) {
			__m128 kernelSamples = azaKernelSample_x4_sse2_rate1(kernel, kernelPos);
			kernelIntegral_x4 = _mm_add_ps(kernelIntegral_x4, kernelSamples);
			kernelPos += 4.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *pSample = src + srcIndex * srcStride + c;
				__m128 srcSamples = _mm_setr_ps(*pSample, *(pSample + srcStride), *(pSample + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride));
				dst[c] += aza_mm_hsum_ps_sse3(_mm_mul_ps(kernelSamples, srcSamples)); // Difference from sse2 version
			}
		}
		if (wrap) {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		} else {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = srcIndex;
				float k = azaKernelSample_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
	} else {
#endif
		if (wrap) {
			for (; srcIndex < minFrame; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
		for (; srcIndex <= endSSE-4; srcIndex+=4) {
			__m128 kernelSamples = azaKernelSample_x4_sse2(kernel, kernelPos, rate);
			kernelIntegral_x4 = _mm_add_ps(kernelIntegral_x4, kernelSamples);
			kernelPos += rate*4.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *pSample = src + srcIndex * srcStride + c;
				__m128 srcSamples = _mm_setr_ps(*pSample, *(pSample + srcStride), *(pSample + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride));
				dst[c] += aza_mm_hsum_ps_sse3(_mm_mul_ps(kernelSamples, srcSamples)); // Difference from sse2 version
			}
		}
		if (wrap) {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		} else {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = srcIndex;
				float k = azaKernelSample(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
#if USE_PACKED_KERNEL
	}
#endif
	kernelIntegral += aza_mm_hsum_ps_sse3(kernelIntegral_x4); // Difference from sse2 version
	if (kernelIntegral > 0.0f) {
		for (int c = 0; c < dstChannels; c++) {
			dst[c] /= kernelIntegral;
		}
	}
}

// We do need a non-fma version of this because FMA is not guaranteed to be there with AVX like it is with AVX2
AZA_SIMD_FEATURES("avx")
void azaSampleWithKernel_avx(float *dst, int dstChannels, azaKernel *kernel, float *src, int srcStride, int minFrame, int maxFrame, bool wrap, int32_t frame, float fraction, float rate) {
	memset(dst, 0, sizeof(*dst)*dstChannels);
	__m256 kernelIntegral_x8 = _mm256_setzero_ps();
	float kernelIntegral = 0.0f;
	int srcStart = frame + (int)ceilf(-(float)kernel->sampleZero / rate);
	int srcLen = (int)ceilf((float)kernel->length / rate) - 1;
	int srcEnd = srcStart + srcLen;
	float kernelPos = rate * (1.0f-fraction) - (float)kernel->sampleZero;
	if (!wrap) {
		int srcStartActual = AZA_CLAMP(srcStart, minFrame, maxFrame-1);
		kernelPos += (float)(srcStartActual - srcStart) * rate;
		srcStart = srcStartActual;
		srcEnd = AZA_CLAMP(srcEnd, minFrame+1, maxFrame);
	}
	int srcIndex = srcStart;
	int endSSE = AZA_MIN(srcEnd, maxFrame);
#if USE_PACKED_KERNEL
	if (rate == 1.0f) {
		if (wrap) {
			for (; srcIndex < minFrame; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
		for (; srcIndex <= endSSE-8; srcIndex+=8) {
			__m256 kernelSamples = azaKernelSample_x8_avx_rate1(kernel, kernelPos);
			kernelIntegral_x8 = _mm256_add_ps(kernelIntegral_x8, kernelSamples);
			kernelPos += 8.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *pSample = src + srcIndex * srcStride + c;
				// For whatever reason, this is the best way to do this.
				__m256 srcSamples_x8 = _mm256_setr_ps(*pSample, *(pSample + srcStride), *(pSample + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride + srcStride + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride + srcStride + srcStride + srcStride + srcStride));
				dst[c] += aza_mm256_hsum_ps(_mm256_mul_ps(kernelSamples, srcSamples_x8));
			}
		}
		for (; srcIndex <= endSSE-4; srcIndex+=4) {
			__m128 kernelSamples = azaKernelSample_x4_sse2_rate1(kernel, kernelPos);
			kernelIntegral_x8 = _mm256_add_ps(kernelIntegral_x8, _mm256_zextps128_ps256(kernelSamples));
			kernelPos += 4.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *base = src + srcIndex * srcStride + c;
				__m128 srcSamples_x4 = _mm_setr_ps(*base, *(base + srcStride), *(base + srcStride + srcStride), *(base + srcStride + srcStride + srcStride));
				dst[c] += aza_mm_hsum_ps_sse3(_mm_mul_ps(kernelSamples, srcSamples_x4));
			}
		}
		if (wrap) {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		} else {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = srcIndex;
				float k = azaKernelSample_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
	} else {
#endif
		if (wrap) {
			for (; srcIndex < minFrame; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
		for (; srcIndex <= endSSE-8; srcIndex+=8) {
			__m256 kernelSamples = azaKernelSample_x8_avx(kernel, kernelPos, rate);
			kernelIntegral_x8 = _mm256_add_ps(kernelIntegral_x8, kernelSamples);
			kernelPos += rate*8.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *pSample = src + srcIndex * srcStride + c;
			#if 0
				float srcSamples[8];
				for (int s = 0; s < 8; s++) {
					srcSamples[s] = *pSample;
					pSample += srcStride;
				}
				__m256 srcSamples_x8 = _mm256_loadu_ps(srcSamples);
			#elif 0
				__m256 srcSamples_x8 = _mm256_setr_ps(*pSample, *(pSample + srcStride), *(pSample + srcStride*2), *(pSample + srcStride*3), *(pSample + srcStride*4), *(pSample + srcStride*5), *(pSample + srcStride*6), *(pSample + srcStride*7));
			#else
				// For whatever reason, this is the best way to do this.
				__m256 srcSamples_x8 = _mm256_setr_ps(*pSample, *(pSample + srcStride), *(pSample + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride + srcStride + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride + srcStride + srcStride + srcStride + srcStride));
			#endif
				dst[c] += aza_mm256_hsum_ps(_mm256_mul_ps(kernelSamples, srcSamples_x8));
			}
		}
		for (; srcIndex <= endSSE-4; srcIndex+=4) {
			__m128 kernelSamples = azaKernelSample_x4_sse2(kernel, kernelPos, rate);
			kernelIntegral_x8 = _mm256_add_ps(kernelIntegral_x8, _mm256_zextps128_ps256(kernelSamples));
			kernelPos += rate*4.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *base = src + srcIndex * srcStride + c;
				__m128 srcSamples_x4 = _mm_setr_ps(*base, *(base + srcStride), *(base + srcStride + srcStride), *(base + srcStride + srcStride + srcStride));
				dst[c] += aza_mm_hsum_ps_sse3(_mm_mul_ps(kernelSamples, srcSamples_x4));
			}
		}
		if (wrap) {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		} else {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = srcIndex;
				float k = azaKernelSample(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
#if USE_PACKED_KERNEL
	}
#endif
	kernelIntegral += aza_mm256_hsum_ps(kernelIntegral_x8);
	if (kernelIntegral > 0.0f) {
		for (int c = 0; c < dstChannels; c++) {
			dst[c] /= kernelIntegral;
		}
	}
}

AZA_SIMD_FEATURES("avx,fma")
void azaSampleWithKernel_avx_fma(float *dst, int dstChannels, azaKernel *kernel, float *src, int srcStride, int minFrame, int maxFrame, bool wrap, int32_t frame, float fraction, float rate) {
	memset(dst, 0, sizeof(*dst)*dstChannels);
	__m256 kernelIntegral_x8 = _mm256_setzero_ps();
	float kernelIntegral = 0.0f;
	int srcStart = frame + (int)ceilf(-(float)kernel->sampleZero / rate);
	int srcLen = (int)ceilf((float)kernel->length / rate) - 1;
	int srcEnd = srcStart + srcLen;
	float kernelPos = rate * (1.0f-fraction) - (float)kernel->sampleZero;
	if (!wrap) {
		int srcStartActual = AZA_CLAMP(srcStart, minFrame, maxFrame-1);
		kernelPos += (float)(srcStartActual - srcStart) * rate;
		srcStart = srcStartActual;
		srcEnd = AZA_CLAMP(srcEnd, minFrame+1, maxFrame);
	}
	int srcIndex = srcStart;
	int endSSE = AZA_MIN(srcEnd, maxFrame);
#if USE_PACKED_KERNEL
	if (rate == 1.0f) {
		if (wrap) {
			for (; srcIndex < minFrame; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample_sse_fma_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
		for (; srcIndex <= endSSE-8; srcIndex+=8) {
			__m256 kernelSamples = azaKernelSample_x8_avx_fma_rate1(kernel, kernelPos);
			kernelIntegral_x8 = _mm256_add_ps(kernelIntegral_x8, kernelSamples);
			kernelPos += 8.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *pSample = src + srcIndex * srcStride + c;
				// For whatever reason, this is the best way to do this.
				__m256 srcSamples_x8 = _mm256_setr_ps(*pSample, *(pSample + srcStride), *(pSample + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride + srcStride + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride + srcStride + srcStride + srcStride + srcStride));
				dst[c] += aza_mm256_hsum_ps(_mm256_mul_ps(kernelSamples, srcSamples_x8));
			}
		}
		for (; srcIndex <= endSSE-4; srcIndex+=4) {
			__m128 kernelSamples = azaKernelSample_x4_sse2_rate1(kernel, kernelPos);
			kernelIntegral_x8 = _mm256_add_ps(kernelIntegral_x8, _mm256_zextps128_ps256(kernelSamples));
			kernelPos += 4.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *base = src + srcIndex * srcStride + c;
				__m128 srcSamples_x4 = _mm_setr_ps(*base, *(base + srcStride), *(base + srcStride + srcStride), *(base + srcStride + srcStride + srcStride));
				dst[c] += aza_mm_hsum_ps_sse3(_mm_mul_ps(kernelSamples, srcSamples_x4));
			}
		}
		if (wrap) {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample_sse_fma_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		} else {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = srcIndex;
				float k = azaKernelSample_sse_fma_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
	} else {
#endif
		if (wrap) {
			for (; srcIndex < minFrame; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample_sse_fma(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
		for (; srcIndex <= endSSE-8; srcIndex+=8) {
			__m256 kernelSamples = azaKernelSample_x8_avx_fma(kernel, kernelPos, rate);
			kernelIntegral_x8 = _mm256_add_ps(kernelIntegral_x8, kernelSamples);
			kernelPos += rate*8.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *pSample = src + srcIndex * srcStride + c;
			#if 0
				float srcSamples[8];
				for (int s = 0; s < 8; s++) {
					srcSamples[s] = *pSample;
					pSample += srcStride;
				}
				__m256 srcSamples_x8 = _mm256_loadu_ps(srcSamples);
			#elif 0
				__m256 srcSamples_x8 = _mm256_setr_ps(*pSample, *(pSample + srcStride), *(pSample + srcStride*2), *(pSample + srcStride*3), *(pSample + srcStride*4), *(pSample + srcStride*5), *(pSample + srcStride*6), *(pSample + srcStride*7));
			#else
				// For whatever reason, this is the most performant way to do this and it's not close.
				__m256 srcSamples_x8 = _mm256_setr_ps(*pSample, *(pSample + srcStride), *(pSample + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride + srcStride + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride + srcStride + srcStride + srcStride + srcStride));
			#endif
				dst[c] += aza_mm256_hsum_ps(_mm256_mul_ps(kernelSamples, srcSamples_x8));
			}
		}
		for (; srcIndex <= endSSE-4; srcIndex+=4) {
			__m128 kernelSamples = azaKernelSample_x4_sse2(kernel, kernelPos, rate);
			kernelIntegral_x8 = _mm256_add_ps(kernelIntegral_x8, _mm256_zextps128_ps256(kernelSamples));
			kernelPos += rate*4.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *pSample = src + srcIndex * srcStride + c;
				__m128 srcSamples_x4 = _mm_setr_ps(*pSample, *(pSample + srcStride), *(pSample + srcStride + srcStride), *(pSample + srcStride + srcStride + srcStride));
				dst[c] += aza_mm_hsum_ps_sse3(_mm_mul_ps(kernelSamples, srcSamples_x4));
			}
		}
		if (wrap) {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample_sse_fma(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		} else {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = srcIndex;
				float k = azaKernelSample_sse_fma(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
#if USE_PACKED_KERNEL
	}
#endif
	kernelIntegral += aza_mm256_hsum_ps(kernelIntegral_x8);
	if (kernelIntegral > 0.0f) {
		for (int c = 0; c < dstChannels; c++) {
			dst[c] /= kernelIntegral;
		}
	}
}

// As explaned in the dispatch function, this is currently not being used even when AVX2 is available, because gather instructions aren't better than individual fetches and in the case of a Ryzen 9 5900x it's actually slower.
AZA_SIMD_FEATURES("avx2,fma")
void azaSampleWithKernel_avx2_fma(float *dst, int dstChannels, azaKernel *kernel, float *src, int srcStride, int minFrame, int maxFrame, bool wrap, int32_t frame, float fraction, float rate) {
	memset(dst, 0, sizeof(*dst)*dstChannels);
	__m256 kernelIntegral_x8 = _mm256_setzero_ps();
	float kernelIntegral = 0.0f;
	int srcStart = frame + (int)ceilf(-(float)kernel->sampleZero / rate);
	int srcLen = (int)ceilf((float)kernel->length / rate) - 1;
	int srcEnd = srcStart + srcLen;
	float kernelPos = rate * (1.0f-fraction) - (float)kernel->sampleZero;
	if (!wrap) {
		int srcStartActual = AZA_CLAMP(srcStart, minFrame, maxFrame-1);
		kernelPos += (float)(srcStartActual - srcStart) * rate;
		srcStart = srcStartActual;
		srcEnd = AZA_CLAMP(srcEnd, minFrame+1, maxFrame);
	}
	int srcIndex = srcStart;
	int endSSE = AZA_MIN(srcEnd, maxFrame);
	__m256i sampleOffsets_x8 = _mm256_mullo_epi32(_mm256_set1_epi32(srcStride), _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7));
#if USE_PACKED_KERNEL
	if (rate == 1.0f) {
		if (wrap) {
			for (; srcIndex < minFrame; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample_sse_fma_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
		for (; srcIndex <= endSSE-8; srcIndex+=8) {
			__m256 kernelSamples = azaKernelSample_x8_avx2_fma_rate1(kernel, kernelPos);
			kernelIntegral_x8 = _mm256_add_ps(kernelIntegral_x8, kernelSamples);
			kernelPos += 8.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *pSample = src + srcIndex * srcStride + c;
				__m256 srcSamples_x8 = _mm256_i32gather_ps(pSample, sampleOffsets_x8, sizeof(float));
				dst[c] += aza_mm256_hsum_ps(_mm256_mul_ps(kernelSamples, srcSamples_x8));
			}
		}
		for (; srcIndex <= endSSE-4; srcIndex+=4) {
			__m128 kernelSamples = azaKernelSample_x4_sse2_rate1(kernel, kernelPos);
			kernelIntegral_x8 = _mm256_add_ps(kernelIntegral_x8, _mm256_zextps128_ps256(kernelSamples));
			kernelPos += 4.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *pSample = src + srcIndex * srcStride + c;
				__m128 srcSamples_x4 = _mm_i32gather_ps(pSample, _mm256_castsi256_si128(sampleOffsets_x8), sizeof(float));
				dst[c] += aza_mm_hsum_ps_sse3(_mm_mul_ps(kernelSamples, srcSamples_x4));
			}
		}
		if (wrap) {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample_sse_fma_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		} else {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = srcIndex;
				float k = azaKernelSample_sse_fma_rate1(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += 1.0f;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
	} else {
#endif
		if (wrap) {
			for (; srcIndex < minFrame; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample_sse_fma(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
		for (; srcIndex <= endSSE-8; srcIndex+=8) {
			__m256 kernelSamples = azaKernelSample_x8_avx2_fma(kernel, kernelPos, rate);
			kernelIntegral_x8 = _mm256_add_ps(kernelIntegral_x8, kernelSamples);
			kernelPos += rate*8.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *pSample = src + srcIndex * srcStride + c;
				__m256 srcSamples_x8 = _mm256_i32gather_ps(pSample, sampleOffsets_x8, sizeof(float));
				dst[c] += aza_mm256_hsum_ps(_mm256_mul_ps(kernelSamples, srcSamples_x8));
			}
		}
		for (; srcIndex <= endSSE-4; srcIndex+=4) {
			__m128 kernelSamples = azaKernelSample_x4_sse2(kernel, kernelPos, rate);
			kernelIntegral_x8 = _mm256_add_ps(kernelIntegral_x8, _mm256_zextps128_ps256(kernelSamples));
			kernelPos += rate*4.0f;
			for (int c = 0; c < dstChannels; c++) {
				float *pSample = src + srcIndex * srcStride + c;
				__m128 srcSamples_x4 = _mm_i32gather_ps(pSample, _mm256_castsi256_si128(sampleOffsets_x8), sizeof(float));
				dst[c] += aza_mm_hsum_ps_sse3(_mm_mul_ps(kernelSamples, srcSamples_x4));
			}
		}
		if (wrap) {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = azaWrapi2(srcIndex, minFrame, maxFrame);
				float k = azaKernelSample_sse_fma(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		} else {
			for (; srcIndex < srcEnd; srcIndex++) {
				int i = srcIndex;
				float k = azaKernelSample_sse_fma(kernel, kernelPos);
				kernelIntegral += k;
				kernelPos += rate;
				for (int c = 0; c < dstChannels; c++) {
					float s = src[i * srcStride + c];
					dst[c] += s * k;
				}
			}
		}
#if USE_PACKED_KERNEL
	}
#endif
	kernelIntegral += aza_mm256_hsum_ps(kernelIntegral_x8);
	if (kernelIntegral > 0.0f) {
		for (int c = 0; c < dstChannels; c++) {
			dst[c] /= kernelIntegral;
		}
	}
}

void azaSampleWithKernel_dispatch(float *dst, int dstChannels, azaKernel *kernel, float *src, int srcStride, int minFrame, int maxFrame, bool wrap, int32_t frame, float fraction, float rate);
void (*azaSampleWithKernel_specialized)(float *dst, int dstChannels, azaKernel *kernel, float *src, int srcStride, int minFrame, int maxFrame, bool wrap, int32_t frame, float fraction, float rate) = azaSampleWithKernel_dispatch;
void azaSampleWithKernel_dispatch(float *dst, int dstChannels, azaKernel *kernel, float *src, int srcStride, int minFrame, int maxFrame, bool wrap, int32_t frame, float fraction, float rate) {
	assert(azaCPUID.initted);
	// NOTE: Gather instructions are ass, don't use them.
	//       On an AMD Ryzen 9 5900x, gather instructions caused a small slowdown compared to individual memory fetches, likely due to fewer pipelining opportunities.
	//       Gather may be better on Intel CPUs, pending testing. If that's the case, we can check for Intel from cpuid and use gather instructions there.
	// if (AZA_AVX2 && AZA_FMA) {
	// 	AZA_LOG_TRACE("choosing azaSampleWithKernel_avx2_fma\n");
	// 	azaSampleWithKernel_specialized = azaSampleWithKernel_avx2_fma;
	// } else
	if (AZA_AVX && AZA_FMA) {
		AZA_LOG_TRACE("choosing azaSampleWithKernel_avx_fma\n");
		azaSampleWithKernel_specialized = azaSampleWithKernel_avx_fma;
	} else
	if (AZA_AVX) {
		AZA_LOG_TRACE("choosing azaSampleWithKernel_avx\n");
		azaSampleWithKernel_specialized = azaSampleWithKernel_avx;
	} else
	if (AZA_SSE3) {
		AZA_LOG_TRACE("choosing azaSampleWithKernel_sse3\n");
		azaSampleWithKernel_specialized = azaSampleWithKernel_sse3;
	} else
	if (AZA_SSE2) {
		AZA_LOG_TRACE("choosing azaSampleWithKernel_sse2\n");
		azaSampleWithKernel_specialized = azaSampleWithKernel_sse2;
	} else
	if (AZA_SSE) {
		AZA_LOG_TRACE("choosing azaSampleWithKernel_sse\n");
		azaSampleWithKernel_specialized = azaSampleWithKernel_sse;
	} else
	{
		AZA_LOG_TRACE("choosing azaSampleWithKernel_scalar\n");
		azaSampleWithKernel_specialized = azaSampleWithKernel_scalar;
	}
	azaSampleWithKernel_specialized(dst, dstChannels, kernel, src, srcStride, minFrame, maxFrame, wrap, frame, fraction, rate);
}

// This only exists for ABI compatibility so we don't export a function pointer that changes
void azaSampleWithKernel(float *dst, int dstChannels, azaKernel *kernel, float *src, int srcStride, int minFrame, int maxFrame, bool wrap, int32_t frame, float fraction, float rate) {
	assert(fraction >= 0.0f && fraction < 1.0f && "fraction should be >= 0 and < 1");
	assert(rate <= 1.0f && "rate should be > 0.01f and <= 1");
	assert(rate > 0.01f && "Are you crazy?!");
	assert(dstChannels >= srcStride);
	azaSampleWithKernel_specialized(dst, dstChannels, kernel, src, srcStride, minFrame, maxFrame, wrap, frame, fraction, rate);
}