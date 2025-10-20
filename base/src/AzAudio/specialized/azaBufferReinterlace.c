/*
	File: azaBufferReinterlace.c
	Author: Philip Haynes
	Specialized implementations of azaBufferReinterlace and dispatch.
	azaBufferReinterlace(2) is declared in dsp.h
*/

#include "../dsp/azaBuffer.h"
#include "../simd.h"

// Fully dynamic fallback

void azaBufferReinterlace_dynamic(azaBuffer *dst, azaBuffer *src) {
	for (uint32_t i = 0; i < dst->frames; i++) {
		for (uint32_t c = 0; c < dst->channelLayout.count; c++) {
			dst->pSamples[i * dst->stride + c] = src->pSamples[c * src->frames + i];
		}
	}
}

// 2 channel specialized reinterlace

void azaBufferReinterlace2Ch_scalar(azaBuffer *dst, azaBuffer *src) {
	float *const src_samples_a = src->pSamples;
	float *const src_samples_b = src->pSamples + src->frames;
	for (uint32_t i = 0, f = 0; i < src->frames; i++) {
		dst->pSamples[f++] = src_samples_a[i];
		dst->pSamples[f++] = src_samples_b[i];
	}
}

AZA_SIMD_FEATURES("sse")
void azaBufferReinterlace2Ch_sse(azaBuffer *dst, azaBuffer *src) {
	int32_t i = 0;
	float *const src_samples_a = src->pSamples;
	float *const src_samples_b = src->pSamples + src->frames;
	for (; i <= (int32_t)src->frames-4; i += 4) {
		int32_t f = i << 1;
		__m128 A0_A1_A2_A3 = _mm_loadu_ps(src_samples_a + i);
		__m128 B0_B1_B2_B3 = _mm_loadu_ps(src_samples_b + i);

		__m128 A0_B0_A1_B1 = _mm_unpacklo_ps(A0_A1_A2_A3, B0_B1_B2_B3);
		__m128 A2_B2_A3_B3 = _mm_unpackhi_ps(A0_A1_A2_A3, B0_B1_B2_B3);

		float *dst_samples = dst->pSamples + f;
		_mm_storeu_ps(dst_samples, A0_B0_A1_B1);
		dst_samples += 4;
		_mm_storeu_ps(dst_samples, A2_B2_A3_B3);
	}
	for (int32_t f = i << 1; i < (int32_t)src->frames; i++) {
		dst->pSamples[f++] = src_samples_a[i];
		dst->pSamples[f++] = src_samples_b[i];
	}
}

AZA_SIMD_FEATURES("avx")
void azaBufferReinterlace2Ch_avx(azaBuffer *dst, azaBuffer *src) {
	int32_t i = 0;
	float *const src_samples_a = src->pSamples;
	float *const src_samples_b = src_samples_a + src->frames;
	for (; i <= (int32_t)src->frames-8; i += 8) {
		int32_t f = i << 1;
		__m256 A0_A1_A2_A3_A4_A5_A6_A7 = _mm256_loadu_ps(src_samples_a + i);
		__m256 B0_B1_B2_B3_B4_B5_B6_B7 = _mm256_loadu_ps(src_samples_b + i);

		__m256 A0_B0_A1_B1_A4_B4_A5_B5 = _mm256_unpacklo_ps(A0_A1_A2_A3_A4_A5_A6_A7, B0_B1_B2_B3_B4_B5_B6_B7);
		__m256 A2_B2_A3_B3_A6_B6_A7_B7 = _mm256_unpackhi_ps(A0_A1_A2_A3_A4_A5_A6_A7, B0_B1_B2_B3_B4_B5_B6_B7);

		__m256 A0_B0_A1_B1_A2_B2_A3_B3 = _mm256_permute2f128_ps(
			A0_B0_A1_B1_A4_B4_A5_B5, A2_B2_A3_B3_A6_B6_A7_B7, _MM256_PERMUTE2F128(0, 2)
		);
		__m256 A4_B4_A5_B5_A6_B6_A7_B7 = _mm256_permute2f128_ps(
			A0_B0_A1_B1_A4_B4_A5_B5, A2_B2_A3_B3_A6_B6_A7_B7, _MM256_PERMUTE2F128(1, 3)
		);

		float *dst_samples = dst->pSamples + f;
		_mm256_storeu_ps(dst_samples, A0_B0_A1_B1_A2_B2_A3_B3);
		dst_samples += 8;
		_mm256_storeu_ps(dst_samples, A4_B4_A5_B5_A6_B6_A7_B7);
	}
	for (int32_t f = i << 1; i < (int32_t)src->frames; i++) {
		dst->pSamples[f++] = src_samples_a[i];
		dst->pSamples[f++] = src_samples_b[i];
	}
}

void azaBufferReinterlace2Ch_dispatch(azaBuffer*, azaBuffer*);
void (*azaBufferReinterlace2Ch)(azaBuffer *dst, azaBuffer *src) = azaBufferReinterlace2Ch_dispatch;
void azaBufferReinterlace2Ch_dispatch(azaBuffer *dst, azaBuffer *src) {
	assert(azaCPUID.initted);
	if (AZA_AVX) {
		azaBufferReinterlace2Ch = azaBufferReinterlace2Ch_avx;
	} else if (AZA_SSE) {
		azaBufferReinterlace2Ch = azaBufferReinterlace2Ch_sse;
	} else {
		azaBufferReinterlace2Ch = azaBufferReinterlace2Ch_scalar;
	}
	azaBufferReinterlace2Ch(dst, src);
}

// 3 channel specialized reinterlace

void azaBufferReinterlace3Ch_scalar(azaBuffer *dst, azaBuffer *src) {
	float *const src_samples_a = src->pSamples;
	float *const src_samples_b = src_samples_a + src->frames;
	float *const src_samples_c = src_samples_b + src->frames;
	for (uint32_t i = 0, f = 0; i < (int32_t)src->frames; i++) {
		dst->pSamples[f++] = src_samples_a[i];
		dst->pSamples[f++] = src_samples_b[i];
		dst->pSamples[f++] = src_samples_c[i];
	}
}

AZA_SIMD_FEATURES("sse")
void azaBufferReinterlace3Ch_sse(azaBuffer *dst, azaBuffer *src) {
	int32_t i = 0;
	float *const src_samples_a = src->pSamples;
	float *const src_samples_b = src_samples_a + src->frames;
	float *const src_samples_c = src_samples_b + src->frames;
	for (; i <= (int32_t)src->frames-4; i += 4) {
		int32_t f = i * 3;
		__m128 A0_A1_A2_A3 = _mm_loadu_ps(src_samples_a + i);
		__m128 B0_B1_B2_B3 = _mm_loadu_ps(src_samples_b + i);
		__m128 C0_C1_C2_C3 = _mm_loadu_ps(src_samples_c + i);

		__m128 A0_B0_A1_B1 = _mm_unpacklo_ps(A0_A1_A2_A3, B0_B1_B2_B3);
		__m128 A2_B2_A3_B3 = _mm_unpackhi_ps(A0_A1_A2_A3, B0_B1_B2_B3);
		__m128 B0_C0_B1_C1 = _mm_unpacklo_ps(B0_B1_B2_B3, C0_C1_C2_C3);
		__m128 B2_C2_B3_C3 = _mm_unpackhi_ps(B0_B1_B2_B3, C0_C1_C2_C3);

		__m128 ______C0_A1 = _mm_unpacklo_ps(B0_C0_B1_C1, A0_A1_A2_A3);
		__m128 C2_A3______ = _mm_unpackhi_ps(C0_C1_C2_C3, A2_B2_A3_B3);

		__m128 A0_B0_C0_A1 = _mm_shuffle_ps(A0_B0_A1_B1, ______C0_A1, _MM_SHUFFLER(0, 1, 2, 3));
		__m128 B1_C1_A2_B2 = _mm_shuffle_ps(B0_C0_B1_C1, A2_B2_A3_B3, _MM_SHUFFLER(2, 3, 0, 1));
		__m128 C2_A3_B3_C3 = _mm_shuffle_ps(C2_A3______, B2_C2_B3_C3, _MM_SHUFFLER(0, 1, 2, 3));

		float *dst_samples = dst->pSamples + f;
		_mm_storeu_ps(dst_samples, A0_B0_C0_A1);
		dst_samples += 4;
		_mm_storeu_ps(dst_samples, B1_C1_A2_B2);
		dst_samples += 4;
		_mm_storeu_ps(dst_samples, C2_A3_B3_C3);
	}
	for (int32_t f = i * 3; i < (int32_t)src->frames; i++) {
		dst->pSamples[f++] = src_samples_a[i];
		dst->pSamples[f++] = src_samples_b[i];
		dst->pSamples[f++] = src_samples_c[i];
	}
}

AZA_SIMD_FEATURES("avx")
void azaBufferReinterlace3Ch_avx(azaBuffer *dst, azaBuffer *src) {
	int32_t i = 0;
	float *const src_samples_a = src->pSamples;
	float *const src_samples_b = src_samples_a + src->frames;
	float *const src_samples_c = src_samples_b + src->frames;
	for (; i <= (int32_t)src->frames-8; i += 8) {
		int32_t f = i * 3;
		__m256 A0_A1_A2_A3_A4_A5_A6_A7 = _mm256_loadu_ps(src_samples_a + i);
		__m256 B0_B1_B2_B3_B4_B5_B6_B7 = _mm256_loadu_ps(src_samples_b + i);
		__m256 C0_C1_C2_C3_C4_C5_C6_C7 = _mm256_loadu_ps(src_samples_c + i);

		// NOTE: We might be able to improve throughput slightly by changing these to vshufps, since we need to do a second shuffle later anyway, it maybe doesn't matter if their order changes?
		__m256 A0_B0_A1_B1_A4_B4_A5_B5 = _mm256_unpacklo_ps(A0_A1_A2_A3_A4_A5_A6_A7, B0_B1_B2_B3_B4_B5_B6_B7);
		__m256 A2_B2_A3_B3_A6_B6_A7_B7 = _mm256_unpackhi_ps(A0_A1_A2_A3_A4_A5_A6_A7, B0_B1_B2_B3_B4_B5_B6_B7);
		__m256 B0_C0_B1_C1_B4_C4_B5_C5 = _mm256_unpacklo_ps(B0_B1_B2_B3_B4_B5_B6_B7, C0_C1_C2_C3_C4_C5_C6_C7);
		__m256 B2_C2_B3_C3_B6_C6_B7_C7 = _mm256_unpackhi_ps(B0_B1_B2_B3_B4_B5_B6_B7, C0_C1_C2_C3_C4_C5_C6_C7);

		__m256 ______C0_A1_______C4_A5 = _mm256_unpacklo_ps(B0_C0_B1_C1_B4_C4_B5_C5, A0_A1_A2_A3_A4_A5_A6_A7);
		__m256 C2_A3_______C6_A7______ = _mm256_unpackhi_ps(C0_C1_C2_C3_C4_C5_C6_C7, A2_B2_A3_B3_A6_B6_A7_B7);

		__m256 A0_B0_C0_A1_A4_B4_C4_A5 = _mm256_shuffle_ps(
			A0_B0_A1_B1_A4_B4_A5_B5, ______C0_A1_______C4_A5, _MM_SHUFFLER(0, 1, 2, 3)
		);
		__m256 B1_C1_A2_B2_B5_C5_A6_B6 = _mm256_shuffle_ps(
			B0_C0_B1_C1_B4_C4_B5_C5, A2_B2_A3_B3_A6_B6_A7_B7, _MM_SHUFFLER(2, 3, 0, 1)
		);
		__m256 C2_A3_B3_C3_C6_A7_B7_C7 = _mm256_shuffle_ps(
			C2_A3_______C6_A7______, B2_C2_B3_C3_B6_C6_B7_C7, _MM_SHUFFLER(0, 1, 2, 3)
		);

		__m256 A0_B0_C0_A1_B1_C1_A2_B2 = _mm256_permute2f128_ps(
			A0_B0_C0_A1_A4_B4_C4_A5, B1_C1_A2_B2_B5_C5_A6_B6, _MM256_PERMUTE2F128(0, 2)
		);
		__m256 C2_A3_B3_C3_A4_B4_C4_A5 = _mm256_permute2f128_ps(
			C2_A3_B3_C3_C6_A7_B7_C7, A0_B0_C0_A1_A4_B4_C4_A5, _MM256_PERMUTE2F128(0, 3)
		);
		__m256 B5_C5_A6_B6_C6_A7_B7_C7 = _mm256_permute2f128_ps(
			B1_C1_A2_B2_B5_C5_A6_B6, C2_A3_B3_C3_C6_A7_B7_C7, _MM256_PERMUTE2F128(1, 3)
		);

		float *dst_samples = dst->pSamples + f;
		_mm256_storeu_ps(dst_samples, A0_B0_C0_A1_B1_C1_A2_B2);
		dst_samples += 8;
		_mm256_storeu_ps(dst_samples, C2_A3_B3_C3_A4_B4_C4_A5);
		dst_samples += 8;
		_mm256_storeu_ps(dst_samples, B5_C5_A6_B6_C6_A7_B7_C7);
	}
	for (int32_t f = i * 3; i < (int32_t)src->frames; i++) {
		dst->pSamples[f++] = src_samples_a[i];
		dst->pSamples[f++] = src_samples_b[i];
		dst->pSamples[f++] = src_samples_c[i];
	}
}

void azaBufferReinterlace3Ch_dispatch(azaBuffer*, azaBuffer*);
void (*azaBufferReinterlace3Ch)(azaBuffer *dst, azaBuffer *src) = azaBufferReinterlace3Ch_dispatch;
void azaBufferReinterlace3Ch_dispatch(azaBuffer *dst, azaBuffer *src) {
	assert(azaCPUID.initted);
	if (AZA_AVX) {
		azaBufferReinterlace3Ch = azaBufferReinterlace3Ch_avx;
	} else if (AZA_SSE) {
		azaBufferReinterlace3Ch = azaBufferReinterlace3Ch_sse;
	} else {
		azaBufferReinterlace3Ch = azaBufferReinterlace3Ch_scalar;
	}
	azaBufferReinterlace3Ch(dst, src);
}

// 4 channel specialized reinterlace

void azaBufferReinterlace4Ch_scalar(azaBuffer *dst, azaBuffer *src) {
	float *const src_samples_a = src->pSamples;
	float *const src_samples_b = src_samples_a + src->frames;
	float *const src_samples_c = src_samples_b + src->frames;
	float *const src_samples_d = src_samples_c + src->frames;
	for (uint32_t i = 0, f = 0; i < dst->frames; i++) {
		dst->pSamples[f++] = src_samples_a[i];
		dst->pSamples[f++] = src_samples_b[i];
		dst->pSamples[f++] = src_samples_c[i];
		dst->pSamples[f++] = src_samples_d[i];
	}
}

AZA_SIMD_FEATURES("sse")
void azaBufferReinterlace4Ch_sse(azaBuffer *dst, azaBuffer *src) {
	int32_t i = 0;
	float *const src_samples_a = src->pSamples;
	float *const src_samples_b = src_samples_a + src->frames;
	float *const src_samples_c = src_samples_b + src->frames;
	float *const src_samples_d = src_samples_c + src->frames;
	for (; i <= (int32_t)src->frames-4; i += 4) {
		int32_t f = i << 2;
		__m128 A0_A1_A2_A3 = _mm_loadu_ps(src_samples_a + i);
		__m128 B0_B1_B2_B3 = _mm_loadu_ps(src_samples_b + i);
		__m128 C0_C1_C2_C3 = _mm_loadu_ps(src_samples_c + i);
		__m128 D0_D1_D2_D3 = _mm_loadu_ps(src_samples_d + i);

		__m128 A0_B0_A1_B1 = _mm_unpacklo_ps(A0_A1_A2_A3, B0_B1_B2_B3);
		__m128 A2_B2_A3_B3 = _mm_unpackhi_ps(A0_A1_A2_A3, B0_B1_B2_B3);
		__m128 C0_D0_C1_D1 = _mm_unpacklo_ps(C0_C1_C2_C3, D0_D1_D2_D3);
		__m128 C2_D2_C3_D3 = _mm_unpackhi_ps(C0_C1_C2_C3, D0_D1_D2_D3);

		__m128 A0_B0_C0_D0 = _mm_movelh_ps(A0_B0_A1_B1, C0_D0_C1_D1);
		__m128 A1_B1_C1_D1 = _mm_movehl_ps(C0_D0_C1_D1, A0_B0_A1_B1);
		__m128 A2_B2_C2_D2 = _mm_movelh_ps(A2_B2_A3_B3, C2_D2_C3_D3);
		__m128 A3_B3_C3_D3 = _mm_movehl_ps(C2_D2_C3_D3, A2_B2_A3_B3);

		float *dst_samples = dst->pSamples + f;
		_mm_storeu_ps(dst_samples, A0_B0_C0_D0);
		dst_samples += 4;
		_mm_storeu_ps(dst_samples, A1_B1_C1_D1);
		dst_samples += 4;
		_mm_storeu_ps(dst_samples, A2_B2_C2_D2);
		dst_samples += 4;
		_mm_storeu_ps(dst_samples, A3_B3_C3_D3);
	}
	for (int32_t f = i << 2; i < (int32_t)src->frames; i++) {
		dst->pSamples[f++] = src_samples_a[i];
		dst->pSamples[f++] = src_samples_b[i];
		dst->pSamples[f++] = src_samples_c[i];
		dst->pSamples[f++] = src_samples_d[i];
	}
}

AZA_SIMD_FEATURES("avx")
void azaBufferReinterlace4Ch_avx(azaBuffer *dst, azaBuffer *src) {
	int32_t i = 0;
	float *const src_samples_a = src->pSamples;
	float *const src_samples_b = src_samples_a + src->frames;
	float *const src_samples_c = src_samples_b + src->frames;
	float *const src_samples_d = src_samples_c + src->frames;
	for (; i <= (int32_t)src->frames-8; i += 8) {
		int32_t f = i << 2;
		__m256 A0_A1_A2_A3_A4_A5_A6_A7 = _mm256_loadu_ps(src_samples_a + i);
		__m256 B0_B1_B2_B3_B4_B5_B6_B7 = _mm256_loadu_ps(src_samples_b + i);
		__m256 C0_C1_C2_C3_C4_C5_C6_C7 = _mm256_loadu_ps(src_samples_c + i);
		__m256 D0_D1_D2_D3_D4_D5_D6_D7 = _mm256_loadu_ps(src_samples_d + i);

		__m256 A0_B0_A1_B1_A4_B4_A5_B5 = _mm256_unpacklo_ps(A0_A1_A2_A3_A4_A5_A6_A7, B0_B1_B2_B3_B4_B5_B6_B7);
		__m256 A2_B2_A3_B3_A6_B6_A7_B7 = _mm256_unpackhi_ps(A0_A1_A2_A3_A4_A5_A6_A7, B0_B1_B2_B3_B4_B5_B6_B7);
		__m256 C0_D0_C1_D1_C4_D4_C5_D5 = _mm256_unpacklo_ps(C0_C1_C2_C3_C4_C5_C6_C7, D0_D1_D2_D3_D4_D5_D6_D7);
		__m256 C2_D2_C3_D3_C6_D6_C7_D7 = _mm256_unpackhi_ps(C0_C1_C2_C3_C4_C5_C6_C7, D0_D1_D2_D3_D4_D5_D6_D7);

		__m256 A0_B0_C0_D0_A4_B4_C4_D4 = _mm256_shuffle_ps(
			A0_B0_A1_B1_A4_B4_A5_B5, C0_D0_C1_D1_C4_D4_C5_D5, _MM_SHUFFLER(0, 1, 0, 1)
		);
		__m256 A1_B1_C1_D1_A5_B5_C5_D5 = _mm256_shuffle_ps(
			A0_B0_A1_B1_A4_B4_A5_B5, C0_D0_C1_D1_C4_D4_C5_D5, _MM_SHUFFLER(2, 3, 2, 3)
		);
		__m256 A2_B2_C2_D2_A6_B6_C6_D6 = _mm256_shuffle_ps(
			A2_B2_A3_B3_A6_B6_A7_B7, C2_D2_C3_D3_C6_D6_C7_D7, _MM_SHUFFLER(0, 1, 0, 1)
		);
		__m256 A3_B3_C3_D3_A7_B7_C7_D7 = _mm256_shuffle_ps(
			A2_B2_A3_B3_A6_B6_A7_B7, C2_D2_C3_D3_C6_D6_C7_D7, _MM_SHUFFLER(2, 3, 2, 3)
		);

		__m256 A0_B0_C0_D0_A1_B1_C1_D1 = _mm256_permute2f128_ps(
			A0_B0_C0_D0_A4_B4_C4_D4, A1_B1_C1_D1_A5_B5_C5_D5, _MM256_PERMUTE2F128(0, 2)
		);
		__m256 A2_B2_C2_D2_A3_B3_C3_D3 = _mm256_permute2f128_ps(
			A2_B2_C2_D2_A6_B6_C6_D6, A3_B3_C3_D3_A7_B7_C7_D7, _MM256_PERMUTE2F128(0, 2)
		);
		__m256 A4_B4_C4_D4_A5_B5_C5_D5 = _mm256_permute2f128_ps(
			A0_B0_C0_D0_A4_B4_C4_D4, A1_B1_C1_D1_A5_B5_C5_D5, _MM256_PERMUTE2F128(1, 3)
		);
		__m256 A6_B6_C6_D6_A7_B7_C7_D7 = _mm256_permute2f128_ps(
			A2_B2_C2_D2_A6_B6_C6_D6, A3_B3_C3_D3_A7_B7_C7_D7, _MM256_PERMUTE2F128(1, 3)
		);

		float *dst_samples = dst->pSamples + f;
		_mm256_storeu_ps(dst_samples, A0_B0_C0_D0_A1_B1_C1_D1);
		dst_samples += 8;
		_mm256_storeu_ps(dst_samples, A2_B2_C2_D2_A3_B3_C3_D3);
		dst_samples += 8;
		_mm256_storeu_ps(dst_samples, A4_B4_C4_D4_A5_B5_C5_D5);
		dst_samples += 8;
		_mm256_storeu_ps(dst_samples, A6_B6_C6_D6_A7_B7_C7_D7);
	}
	for (int32_t f = i << 2; i < (int32_t)src->frames; i++) {
		dst->pSamples[f++] = src_samples_a[i];
		dst->pSamples[f++] = src_samples_b[i];
		dst->pSamples[f++] = src_samples_c[i];
		dst->pSamples[f++] = src_samples_d[i];
	}
}

void azaBufferReinterlace4Ch_dispatch(azaBuffer*, azaBuffer*);
void (*azaBufferReinterlace4Ch)(azaBuffer *dst, azaBuffer *src) = azaBufferReinterlace4Ch_dispatch;
void azaBufferReinterlace4Ch_dispatch(azaBuffer *dst, azaBuffer *src) {
	assert(azaCPUID.initted);
	if (AZA_AVX) {
		azaBufferReinterlace4Ch = azaBufferReinterlace4Ch_avx;
	} else if (AZA_SSE) {
		azaBufferReinterlace4Ch = azaBufferReinterlace4Ch_sse;
	} else {
		azaBufferReinterlace4Ch = azaBufferReinterlace4Ch_scalar;
	}
	azaBufferReinterlace4Ch(dst, src);
}

// Specialization dispatch

void azaBufferReinterlace(azaBuffer *dst, azaBuffer *src) {
	assert(dst->frames == src->frames);
	// We don't actually have to worry about src->stride because we just kinda ignore it anyway
	// assert(src->stride == src->channelLayout.count);
	assert(dst->channelLayout.count == src->channelLayout.count);
	if (dst->stride == dst->channelLayout.count) {
		switch (dst->channelLayout.count) {
			case 0:
			case 1:
				azaBufferCopy(dst, src);
				return;
			case 2:
				azaBufferReinterlace2Ch(dst, src);
				return;
			case 3:
				azaBufferReinterlace3Ch(dst, src);
				return;
			case 4:
				azaBufferReinterlace4Ch(dst, src);
				return;
			// TODO: Specialized implementations up to 8 channels would be nice, as 7.1 is fairly common. Beyond that, maybe just non-dynamic scalar implementations would still be good? The dynamic fallback is pretty bad...
			default:
				azaBufferReinterlace_dynamic(dst, src);
				return;
		}
	}
	if (dst->channelLayout.count <= 1) {
		azaBufferCopy(dst, src);
	} else {
		azaBufferReinterlace_dynamic(dst, src);
	}
}