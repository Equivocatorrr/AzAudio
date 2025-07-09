/*
	File: azaKernel.c
	Author: Philip Haynes
	Specialized implementations of azaKernel procedures and dispatch.
	Non-specialized code still lives in dsp.c
	Implements the following (declared in dsp.h):
		- azaKernelSample(3)
		- azaSampleWithKernel(6)
*/

#include "../dsp.h"
#include "../simd.h"
#include "../helpers.h"
#include "../AzAudio.h"

float azaKernelSample(azaKernel *kernel, int i, float pos) {
	float x = (float)(i + kernel->sampleZero) - pos;
	int32_t index = (int32_t)x;
	if (index >= kernel->length-1 || index < 0) return 0.0f;
	x -= (float)index;
	x *= kernel->scale;
	assert(x >= 0.0f);
	int32_t subsample = (int32_t)x;
	x -= (float)subsample;
	assert(subsample < kernel->scale);
	float *srcSubsample0 = kernel->packed + ((subsample+0) * kernel->length);
	float *srcSubsample1 = kernel->packed + ((subsample+1) * kernel->length);
	float result = azaLerpf(srcSubsample0[index], srcSubsample1[index], x);
	// index = index * kernel->scale + subsample;
	// float result = azaLerpf(kernel->table[index], kernel->table[index+1], x);
	return result;
}

AZA_SIMD_FEATURES("sse,fma")
float azaKernelSample_sse_fma(azaKernel *kernel, int i, float pos) {
	float x = (float)(i + kernel->sampleZero) - pos;
	int32_t index = (int32_t)x;
	if (index >= kernel->length-1 || index < 0) return 0.0f;
	x -= (float)index;
	x *= kernel->scale;
	assert(x >= 0.0f);
	int32_t subsample = (int32_t)x;
	x -= (float)subsample;
	assert(subsample < kernel->scale);
	float *srcSubsample0 = kernel->packed + ((subsample+0) * kernel->length);
	float *srcSubsample1 = kernel->packed + ((subsample+1) * kernel->length);
	float result = azaLerpf_sse_fma(srcSubsample0[index], srcSubsample1[index], x);
	// index = index * kernel->scale + subsample;
	// float result = azaLerpf(kernel->table[index], kernel->table[index+1], x);
	return result;
}

AZA_SIMD_FEATURES("sse")
static __m128 azaKernelSample_x4_sse(azaKernel *kernel, int i, float pos) {
	float x = (float)(i + kernel->sampleZero) - pos;
	int32_t index = (int32_t)x;
	// We won't be doing any masking, so don't be calling this if you don't handle tails as scalars!
	assert(index <= kernel->length-4);
	assert(index >= 0);
	x -= (float)index;
	x *= kernel->scale;
	assert(x >= 0.0f);
	int32_t subsample = (int32_t)x;
	x -= (float)subsample;
	assert(subsample < kernel->scale);
	float *srcSubsample0 = kernel->packed + ((subsample+0) * kernel->length);
	float *srcSubsample1 = kernel->packed + ((subsample+1) * kernel->length);
	__m128 samples0 = _mm_loadu_ps(srcSubsample0 + index);
	__m128 samples1 = _mm_loadu_ps(srcSubsample1 + index);
	__m128 result = azaLerp_x4_sse(samples0, samples1, _mm_set1_ps(x));
	return result;
}

AZA_SIMD_FEATURES("avx,fma")
static __m256 azaKernelSample_x8_avx_fma(azaKernel *kernel, int i, float pos) {
	float x = (float)(i + kernel->sampleZero) - pos;
	int32_t index = (int32_t)x;
	// We won't be doing any masking, so don't be calling this if you don't handle tails as scalars!
	assert(index <= kernel->length-8);
	assert(index >= 0);
	x -= (float)index;
	x *= kernel->scale;
	assert(x >= 0.0f);
	int32_t subsample = (int32_t)x;
	x -= (float)subsample;
	assert(subsample < kernel->scale);
	float *srcSubsample0 = kernel->packed + ((subsample+0) * kernel->length);
	float *srcSubsample1 = kernel->packed + ((subsample+1) * kernel->length);
	__m256 samples0 = _mm256_loadu_ps(srcSubsample0 + index);
	__m256 samples1 = _mm256_loadu_ps(srcSubsample1 + index);
	__m256 result = azaLerp_x8_avx_fma(samples0, samples1, _mm256_set1_ps(x));
	return result;
}

float azaSampleWithKernel_scalar(float *src, int stride, int minFrame, int maxFrame, azaKernel *kernel, float pos) {
	float result = 0.0f;
	int start, end;
	start = (int)pos - kernel->sampleZero + 1;
	end = start + kernel->length;
	int i = start;
	// TODO: I've noticed from some null testing between Lanczos kernel sizes (radius 64 and 32, both resolution 128) that the peak output difference was on the order of -33db. I believe this is probably too high, possibly indicating a bug in this kernel sampling code (off-by-one error?). We need to develop some rigorous testing and diagnostics tools to streamline and quantify these barely-audible issues. Probably do some math to figure out what the expected difference between kernels should be too, since I'm basically just guessing it's high.
	for (; i < end; i++) {
		int index = AZA_CLAMP(i, minFrame, maxFrame-1);
		float s = src[index * stride];
		result += s * azaKernelSample(kernel, i, pos);
	}
	return result;
}

AZA_SIMD_FEATURES("sse")
float azaSampleWithKernel_sse(float *src, int stride, int minFrame, int maxFrame, azaKernel *kernel, float pos) {
	float result = 0.0f;
	int start, end;
	start = (int)pos - kernel->sampleZero + 1;
	end = start + kernel->length;
	int i = start;
	if (stride == 1 && i >= minFrame && i+4 < maxFrame) {
		__m128 result_x4 = _mm_setzero_ps();
		for (; i <= end-4; i += 4) {
			__m128 kernelSamples = azaKernelSample_x4_sse(kernel, i, pos);
			__m128 srcSamples = _mm_loadu_ps(src + i);
			result_x4 = _mm_add_ps(_mm_mul_ps(srcSamples, kernelSamples), result_x4);
		}
		float hsum = aza_mm_hsum_ps_sse(result_x4);
		result = hsum;
	}
	for (; i < end; i++) {
		int index = AZA_CLAMP(i, minFrame, maxFrame-1);
		float s = src[index * stride];
		result += s * azaKernelSample(kernel, i, pos);
	}
	return result;
}

AZA_SIMD_FEATURES("avx,fma")
float azaSampleWithKernel_avx_fma(float *src, int stride, int minFrame, int maxFrame, azaKernel *kernel, float pos) {
	float result = 0.0f;
	int start, end;
	start = (int)pos - kernel->sampleZero + 1;
	end = start + kernel->length;
	int i = start;
	// TODO: I've noticed from some null testing between Lanczos kernel sizes (radius 64 and 32, both resolution 128) that the peak output difference was on the order of -33db. I believe this is probably too high, possibly indicating a bug in this kernel sampling code (off-by-one error?). We need to develop some rigorous testing and diagnostics tools to streamline and quantify these barely-audible issues. Probably do some math to figure out what the expected difference between kernels should be too, since I'm basically just guessing it's high.
	// This AVX implementation produces the exact same results as the scalar version down to the last ULP.
	if (stride == 1 && i >= minFrame && i+8 < maxFrame) {
		// We actually lose performance here if both sets of samples are randomly fetched, because azaKernelSample_x8 has additional logic.
		__m256 result_x8 = _mm256_setzero_ps();
		for (; i <= end-8; i += 8) {
			__m256 kernelSamples = azaKernelSample_x8_avx_fma(kernel, i, pos);
			__m256 srcSamples = _mm256_loadu_ps(src + i);
#if 0 // For debugging purposes only
			float_x8 kernelSamples2;
			for (int j = 0; j < 8; j++) {
				kernelSamples2.f[j] = azaKernelSample(kernel, i+j, pos);
			}
			float_x8 diff;
			diff.v = _mm256_sub_ps(kernelSamples, kernelSamples2.v);
			if (
				diff.f[0] != 0.0f ||
				diff.f[1] != 0.0f ||
				diff.f[2] != 0.0f ||
				diff.f[3] != 0.0f ||
				diff.f[4] != 0.0f ||
				diff.f[5] != 0.0f ||
				diff.f[6] != 0.0f ||
				diff.f[7] != 0.0f
			) {
				__m256 kernelSamples3 = azaKernelSample_x8(kernel, i, pos);
				for (int j = 0; j < 8; j++) {
					float kernelSamples4 = azaKernelSample(kernel, i+j, pos);
				}
			}
#endif
			result_x8 = _mm256_fmadd_ps(srcSamples, kernelSamples, result_x8);
		}
		float hsum = aza_mm256_hsum_ps(result_x8);
		result = hsum;
	}
	for (; i < end; i++) {
		int index = AZA_CLAMP(i, minFrame, maxFrame-1);
		float s = src[index * stride];
		result = aza_fmadd_f32(s, azaKernelSample_sse_fma(kernel, i, pos), result);
	}
	return result;
}

float azaSampleWithKernel_dispatch(float *src, int stride, int minFrame, int maxFrame, azaKernel *kernel, float pos);
float (*azaSampleWithKernel_specialized)(float *src, int stride, int minFrame, int maxFrame, azaKernel *kernel, float pos) = azaSampleWithKernel_dispatch;
float azaSampleWithKernel_dispatch(float *src, int stride, int minFrame, int maxFrame, azaKernel *kernel, float pos) {
	assert(azaCPUID.initted);
	if (AZA_AVX && AZA_FMA) {
		AZA_LOG_TRACE("choosing azaSampleWithKernel_avx_fma\n");
		azaSampleWithKernel_specialized = azaSampleWithKernel_avx_fma;
	} else if (AZA_SSE) {
		AZA_LOG_TRACE("choosing azaSampleWithKernel_sse\n");
		azaSampleWithKernel_specialized = azaSampleWithKernel_sse;
	} else {
		AZA_LOG_TRACE("choosing azaSampleWithKernel_scalar\n");
		azaSampleWithKernel_specialized = azaSampleWithKernel_scalar;
	}
	return azaSampleWithKernel_specialized(src, stride, minFrame, maxFrame, kernel, pos);
}

// This only exists for ABI compatibility so we don't export a function pointer that changes
// Normally this is where we'd throw any validation code, but we don't have any apparently.
float azaSampleWithKernel(float *src, int stride, int minFrame, int maxFrame, azaKernel *kernel, float pos) {
	return azaSampleWithKernel_specialized(src, stride, minFrame, maxFrame, kernel, pos);
}