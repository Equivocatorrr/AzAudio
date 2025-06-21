/*
	File: azaKernel.c
	Author: Philip Haynes
	Specialized implementations of azaKernel procedures and dispatch.
	Non-specialized code still lives in dsp.c
	Implements the following (declared in dps.h):
		- azaKernelSample(3)
		- azaSampleWithKernel(6)
*/

#include "../dsp.h"
#include "../simd.h"
#include "../helpers.h"

float azaKernelSample(azaKernel *kernel, int i, float pos) {
	float x = (float)i - pos;
	if (kernel->isSymmetrical) {
		if (x < 0.0f) x = -x;
	} else {
		if (x < 0.0f) return 0.0f;
	}
	x *= kernel->scale;
	uint32_t index = (uint32_t)x;
	if (index >= kernel->size-1) return 0.0f;
	x -= (float)index;
	float result = azaLerpf(kernel->table[index], kernel->table[index+1], x);
	return result;
}

AZA_SIMD_FEATURES("avx,fma")
static __m256 azaKernelSample_x8_avx_fma(azaKernel *kernel, int i, float pos) {
	int step;
	float x = (float)i - pos;
	if (kernel->isSymmetrical) {
		if (x < 0.0f) {
			if (x > -8.0f) {
				// Have to handle the pivot point
				x = -x * kernel->scale;
				int32_t index = (int32_t)x;
				x -= (float)index;
				float_x8 a = {0};
				float_x8 b = {0};
				float_x8 t = {0};
				step = -(int)kernel->scale;
				for (uint32_t j = 0; j < 8; j++) {
					if ((uint32_t)index < kernel->size-1) {
						a.f[j] = kernel->table[index];
						b.f[j] = kernel->table[index+1];
						t.f[j] = x;
					}
					index += step;
					if (index < 0) {
						step = -step;
						// Gotta do it like this to get the last ULP of error out.
						x = (float)(i+j+1) - pos;
						x *= kernel->scale;
						index = (int32_t)x;
						x -= (float)index;
					}
				}
				return azaLerp_x8_avx_fma(a.v, b.v, t.v);
			} else {
				// Send it in reverse
				x = -x;
				step = -(int)kernel->scale;
			}
		} else {
			step = (int)kernel->scale;
		}
	} else {
		if (x <= -8.0f) return _mm256_setzero_ps();
		step = (int)kernel->scale;
	}
	// Send it forward
	x *= kernel->scale;
	uint32_t index = (uint32_t)x;
	if (index >= kernel->size-1 && step > 0) return _mm256_setzero_ps();
	x -= (float)index;
	float_x8 a = {0};
	float_x8 b = {0};
	for (uint32_t j = 0; j < 8; j++) {
		if (index < kernel->size-1) {
			a.f[j] = kernel->table[index];
			b.f[j] = kernel->table[index+1];
		}
		index += step;
	}
	float_x8 result;
	result.v = azaLerp_x8_avx_fma(a.v, b.v, _mm256_set1_ps(x));
	return result.v;
}

float azaSampleWithKernel_scalar(float *src, int stride, int minFrame, int maxFrame, azaKernel *kernel, float pos) {
	float result = 0.0f;
	int start, end;
	if (kernel->isSymmetrical) {
		start = (int)pos - (int)kernel->length + 1;
		end = (int)pos + (int)kernel->length;
	} else {
		start = (int)pos;
		end = (int)pos + (int)kernel->length;
	}
	int i = start;
	// TODO: I've noticed from some null testing between Lanczos kernel sizes (radius 64 and 32, both resolution 128) that the peak output difference was on the order of -33db. I believe this is probably too high, possibly indicating a bug in this kernel sampling code (off-by-one error?). We need to develop some rigorous testing and diagnostics tools to streamline and quantify these barely-audible issues. Probably do some math to figure out what the expected difference between kernels should be too, since I'm basically just guessing it's high.
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
	if (kernel->isSymmetrical) {
		start = (int)pos - (int)kernel->length + 1;
		end = (int)pos + (int)kernel->length;
	} else {
		start = (int)pos;
		end = (int)pos + (int)kernel->length;
	}
	int i = start;
	// TODO: I've noticed from some null testing between Lanczos kernel sizes (radius 64 and 32, both resolution 128) that the peak output difference was on the order of -33db. I believe this is probably too high, possibly indicating a bug in this kernel sampling code (off-by-one error?). We need to develop some rigorous testing and diagnostics tools to streamline and quantify these barely-audible issues. Probably do some math to figure out what the expected difference between kernels should be too, since I'm basically just guessing it's high.
	// This AVX implementation produces the exact same results as the scalar version down to the last ULP.
	if (stride == 1 && i >= minFrame && i+8 < maxFrame) {
		// We actually lose performance here if both sets of samples are randomly fetched, because azaKernelSample_x8 has additional logic.
		for (; i <= end-8; i += 8) {
			__m256 srcSamples;
			srcSamples = _mm256_loadu_ps(src + i);
			__m256 kernelSamples = azaKernelSample_x8_avx_fma(kernel, i, pos);
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
			float hsum = aza_mm256_hsum_ps(_mm256_mul_ps(srcSamples, kernelSamples));
			result += hsum;
		}
	}
	for (; i < end; i++) {
		int index = AZA_CLAMP(i, minFrame, maxFrame-1);
		float s = src[index * stride];
		result += s * azaKernelSample(kernel, i, pos);
	}
	return result;
}

float azaSampleWithKernel_dispatch(float *src, int stride, int minFrame, int maxFrame, azaKernel *kernel, float pos);
float (*azaSampleWithKernel_specialized)(float *src, int stride, int minFrame, int maxFrame, azaKernel *kernel, float pos) = azaSampleWithKernel_dispatch;
float azaSampleWithKernel_dispatch(float *src, int stride, int minFrame, int maxFrame, azaKernel *kernel, float pos) {
	assert(azaCPUID.initted);
	if (AZA_AVX && AZA_FMA) {
		azaSampleWithKernel_specialized = azaSampleWithKernel_avx_fma;
	// } else if (AZA_SSE) {
	// 	azaSampleWithKernel_specialized = azaSampleWithKernel_sse;
	} else {
		azaSampleWithKernel_specialized = azaSampleWithKernel_scalar;
	}
	return azaSampleWithKernel_specialized(src, stride, minFrame, maxFrame, kernel, pos);
}

// This only exists for ABI compatibility so we don't export a function pointer that changes
// Normally this is where we'd throw any validation code, but we don't have any apparently.
float azaSampleWithKernel(float *src, int stride, int minFrame, int maxFrame, azaKernel *kernel, float pos) {
	return azaSampleWithKernel_specialized(src, stride, minFrame, maxFrame, kernel, pos);
}