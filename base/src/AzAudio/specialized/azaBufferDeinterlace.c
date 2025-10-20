/*
	File: azaBufferDeinterlace.c
	Author: Philip Haynes
	Specialized implementations of azaBufferDeinterlace and dispatch.
	azaBufferDeinterlace(2) is declared in dsp.h
*/

#include "../dsp/azaBuffer.h"
#include "../simd.h"

// Fully dynamic fallback

void azaBufferDeinterlace_dynamic(azaBuffer *dst, azaBuffer *src) {
	for (uint32_t i = 0; i < dst->frames; i++) {
		for (uint32_t c = 0; c < dst->channelLayout.count; c++) {
			dst->pSamples[c * dst->frames + i] = src->pSamples[i * src->stride + c];
		}
	}
}

// 2 channel specialized deinterlace

// NOTE: SSE+SSE2 are guaranteed to be available on x86_64. Is there still any demand for 32-bit x86 support? I guess it doesn't cost us any more than we're already paying to support it.
void azaBufferDeinterlace2Ch_scalar(azaBuffer *dst, azaBuffer *src) {
	float *const dst_samples_a = dst->pSamples;
	float *const dst_samples_b = dst_samples_a + dst->frames;
	for (uint32_t i = 0, f = 0; i < dst->frames; i++) {
		dst_samples_a[i] = src->pSamples[f++];
		dst_samples_b[i] = src->pSamples[f++];
	}
}

AZA_SIMD_FEATURES("sse")
void azaBufferDeinterlace2Ch_sse(azaBuffer *dst, azaBuffer *src) {
	int32_t i = 0;
	float *const dst_samples_a = dst->pSamples;
	float *const dst_samples_b = dst_samples_a + dst->frames;
	for (; i <= (int32_t)src->frames-4; i += 4) {
		int32_t f = i << 1;
		float *src_samples = src->pSamples + f;
		// _mm_prefetch((const char*)(src_samples + 32), _MM_HINT_NTA);
		__m128 A0_B0_A1_B1 = _mm_loadu_ps(src_samples);
		src_samples += 4;
		__m128 A2_B2_A3_B3 = _mm_loadu_ps(src_samples);


		__m128 A0_A1_B0_B1 = _mm_shuffle_ps(A0_B0_A1_B1, A0_B0_A1_B1, _MM_SHUFFLER(0, 2, 1, 3));
		__m128 A2_A3_B2_B3 = _mm_shuffle_ps(A2_B2_A3_B3, A2_B2_A3_B3, _MM_SHUFFLER(0, 2, 1, 3));

		__m128 A0_A1_A2_A3 = _mm_movelh_ps(A0_A1_B0_B1, A2_A3_B2_B3);
		__m128 B0_B1_B2_B3 = _mm_movehl_ps(A2_A3_B2_B3, A0_A1_B0_B1);

		_mm_storeu_ps(dst_samples_a + i, A0_A1_A2_A3);
		_mm_storeu_ps(dst_samples_b + i, B0_B1_B2_B3);
	}
	for (int32_t f = i << 1; i < (int32_t)src->frames; i++) {
		dst_samples_a[i] = src->pSamples[f++];
		dst_samples_b[i] = src->pSamples[f++];
	}
}

AZA_SIMD_FEATURES("avx")
void azaBufferDeinterlace2Ch_avx(azaBuffer *dst, azaBuffer *src) {
	int32_t i = 0;
	float *const dst_samples_a = dst->pSamples;
	float *const dst_samples_b = dst_samples_a + dst->frames;
	for (; i <= (int32_t)src->frames-8; i += 8) {
		int32_t f = i << 1;
		float *src_samples = src->pSamples + f;
		__m256 A0_B0_A1_B1_A2_B2_A3_B3 = _mm256_loadu_ps(src_samples);
		src_samples += 8;
		__m256 A4_B4_A5_B5_A6_B6_A7_B7 = _mm256_loadu_ps(src_samples);

		// This implementation is AVX because we dropped vpermps in favor of the gigabrained shuffle (turns out the ISA designers knew what they were doing)
		// This is also marginally faster because we cross 128-bit boundaries only once, whereas the AVX2 one above crosses them twice.
		__m256 A0_B0_A1_B1_A4_B4_A5_B5 = _mm256_permute2f128_ps(
			A0_B0_A1_B1_A2_B2_A3_B3, A4_B4_A5_B5_A6_B6_A7_B7, _MM256_PERMUTE2F128(0, 2)
		);
		__m256 A2_B2_A3_B3_A6_B6_A7_B7 = _mm256_permute2f128_ps(
			A0_B0_A1_B1_A2_B2_A3_B3, A4_B4_A5_B5_A6_B6_A7_B7, _MM256_PERMUTE2F128(1, 3)
		);
		__m256 A0_A1_A2_A3_A4_A5_A6_A7 = _mm256_shuffle_ps(A0_B0_A1_B1_A4_B4_A5_B5, A2_B2_A3_B3_A6_B6_A7_B7, _MM_SHUFFLER(0, 2, 0, 2));
		__m256 B0_B1_B2_B3_B4_B5_B6_B7 = _mm256_shuffle_ps(A0_B0_A1_B1_A4_B4_A5_B5, A2_B2_A3_B3_A6_B6_A7_B7, _MM_SHUFFLER(1, 3, 1, 3));

		_mm256_storeu_ps(dst_samples_a + i, A0_A1_A2_A3_A4_A5_A6_A7);
		_mm256_storeu_ps(dst_samples_b + i, B0_B1_B2_B3_B4_B5_B6_B7);
	}
	for (int32_t f = i << 1; i < (int32_t)src->frames; i++) {
		dst_samples_a[i] = src->pSamples[f++];
		dst_samples_b[i] = src->pSamples[f++];
	}
}

// TODO: delete this because the avx one is simpler and faster
AZA_SIMD_FEATURES("avx2")
void azaBufferDeinterlace2Ch_avx2(azaBuffer *dst, azaBuffer *src) {
	int32_t i = 0;
	__m256i permute = _mm256_setr_epi32(0, 2, 4, 6, 1, 3, 5, 7);
	float *const dst_samples_a = dst->pSamples;
	float *const dst_samples_b = dst_samples_a + dst->frames;
	for (; i <= (int32_t)src->frames-8; i += 8) {
		int32_t f = i << 1;
		float *src_samples = src->pSamples + f;
		__m256 A0_B0_A1_B1_A2_B2_A3_B3 = _mm256_loadu_ps(src_samples);
		src_samples += 8;
		__m256 A4_B4_A5_B5_A6_B6_A7_B7 = _mm256_loadu_ps(src_samples);

		__m256 A0_A1_A2_A3_B0_B1_B2_B3 = _mm256_permutevar8x32_ps(A0_B0_A1_B1_A2_B2_A3_B3, permute);
		__m256 A4_A5_A6_A7_B4_B5_B6_B7 = _mm256_permutevar8x32_ps(A4_B4_A5_B5_A6_B6_A7_B7, permute);
		__m256 A0_A1_A2_A3_A4_A5_A6_A7 = _mm256_permute2f128_ps(
			A0_A1_A2_A3_B0_B1_B2_B3, A4_A5_A6_A7_B4_B5_B6_B7, _MM256_PERMUTE2F128(0, 2)
		);
		__m256 B0_B1_B2_B3_B4_B5_B6_B7 = _mm256_permute2f128_ps(
			A0_A1_A2_A3_B0_B1_B2_B3, A4_A5_A6_A7_B4_B5_B6_B7, _MM256_PERMUTE2F128(1, 3)
		);

		_mm256_storeu_ps(dst_samples_a + i, A0_A1_A2_A3_A4_A5_A6_A7);
		_mm256_storeu_ps(dst_samples_b + i, B0_B1_B2_B3_B4_B5_B6_B7);
	}
	for (int32_t f = i << 1; i < (int32_t)src->frames; i++) {
		dst_samples_a[i] = src->pSamples[f++];
		dst_samples_b[i] = src->pSamples[f++];
	}
}

void azaBufferDeinterlace2Ch_dispatch(azaBuffer*, azaBuffer*);
void (*azaBufferDeinterlace2Ch)(azaBuffer *dst, azaBuffer *src) = azaBufferDeinterlace2Ch_dispatch;
void azaBufferDeinterlace2Ch_dispatch(azaBuffer *dst, azaBuffer *src) {
	assert(azaCPUID.initted);
	if (AZA_AVX) {
		azaBufferDeinterlace2Ch = azaBufferDeinterlace2Ch_avx;
	} else if (AZA_SSE) {
		azaBufferDeinterlace2Ch = azaBufferDeinterlace2Ch_sse;
	} else {
		azaBufferDeinterlace2Ch = azaBufferDeinterlace2Ch_scalar;
	}
	azaBufferDeinterlace2Ch(dst, src);
}

// 3 channel specialized deinterlace

void azaBufferDeinterlace3Ch_scalar(azaBuffer *dst, azaBuffer *src) {
	float *const dst_samples_a = dst->pSamples;
	float *const dst_samples_b = dst_samples_a + dst->frames;
	float *const dst_samples_c = dst_samples_b + dst->frames;
	for (uint32_t i = 0, f = 0; i < dst->frames; i++) {
		dst_samples_a[i] = src->pSamples[f++];
		dst_samples_b[i] = src->pSamples[f++];
		dst_samples_c[i] = src->pSamples[f++];
	}
}

AZA_SIMD_FEATURES("sse")
void azaBufferDeinterlace3Ch_sse(azaBuffer *dst, azaBuffer *src) {
	int32_t i = 0;
	float *const dst_samples_a = dst->pSamples;
	float *const dst_samples_b = dst_samples_a + dst->frames;
	float *const dst_samples_c = dst_samples_b + dst->frames;
	for (; i <= (int32_t)src->frames-4; i += 4) {
		int32_t f = i * 3;
		float *src_samples = src->pSamples + f;
		__m128 A0_B0_C0_A1 = _mm_loadu_ps(src_samples);
		src_samples += 4;
		__m128 B1_C1_A2_B2 = _mm_loadu_ps(src_samples);
		src_samples += 4;
		__m128 C2_A3_B3_C3 = _mm_loadu_ps(src_samples);
		// This sure was something to figure out. Now I'm wondering about writing a script to work this stuff out, possibly just with brute force.
		// TODO: It's probably possible to use unpack for this as well so we can eliminate some shuffles.
		__m128 A0_A1_C0_B0 = _mm_shuffle_ps(A0_B0_C0_A1, A0_B0_C0_A1, _MM_SHUFFLER(0, 3, 2, 1));
		__m128 C1_A2_B1_B2 = _mm_shuffle_ps(B1_C1_A2_B2, B1_C1_A2_B2, _MM_SHUFFLER(1, 2, 0, 3));
		__m128 C1_A2_A0_A1 = _mm_movelh_ps(C1_A2_B1_B2, A0_A1_C0_B0);
		__m128 A3____C2_C3 = _mm_shuffle_ps(C2_A3_B3_C3, C2_A3_B3_C3, _MM_SHUFFLER(1, 0, 0, 3));
		__m128 A3_A2_A0_A1 = _mm_move_ss(C1_A2_A0_A1, A3____C2_C3);
		__m128 C0_B0_B1_B2 = _mm_movehl_ps(C1_A2_B1_B2, A0_A1_C0_B0);
		__m128 B3_________ = _mm_shuffle_ps(C2_A3_B3_C3, C2_A3_B3_C3, _MM_SHUFFLER(2, 0, 0, 0));
		__m128 B3_B0_B1_B2 = _mm_move_ss(C0_B0_B1_B2, B3_________);
		__m128 C1____C2_C3 = _mm_move_ss(A3____C2_C3, C1_A2_A0_A1);
		__m128 ___C1_C2_C3 = _mm_shuffle_ps(C1____C2_C3, C1____C2_C3, _MM_SHUFFLER(1, 0, 2, 3));

		__m128 A0_A1_A2_A3 = _mm_shuffle_ps(A3_A2_A0_A1, A3_A2_A0_A1, _MM_SHUFFLER(2, 3, 1, 0));
		__m128 B0_B1_B2_B3 = _mm_shuffle_ps(B3_B0_B1_B2, B3_B0_B1_B2, _MM_SHUFFLER(1, 2, 3, 0));
		__m128 C0_C1_C2_C3 = _mm_move_ss(___C1_C2_C3, C0_B0_B1_B2);

		_mm_storeu_ps(dst_samples_a + i, A0_A1_A2_A3);
		_mm_storeu_ps(dst_samples_b + i, B0_B1_B2_B3);
		_mm_storeu_ps(dst_samples_c + i, C0_C1_C2_C3);
	}
	for (int32_t f = i * 3; i < (int32_t)src->frames; i++) {
		dst_samples_a[i] = src->pSamples[f++];
		dst_samples_b[i] = src->pSamples[f++];
		dst_samples_c[i] = src->pSamples[f++];
	}
}

// TODO: Determine whether to keep this version as its performance is just BARELY better on a ryzen 9 5900x vs sse (wouldn't be too surprised to see regressions on other CPUs)
AZA_SIMD_FEATURES("sse4.1")
void azaBufferDeinterlace3Ch_sse4_1(azaBuffer *dst, azaBuffer *src) {
	int32_t i = 0;
	float *const dst_samples_a = dst->pSamples;
	float *const dst_samples_b = dst_samples_a + dst->frames;
	float *const dst_samples_c = dst_samples_b + dst->frames;
	for (; i <= (int32_t)src->frames-4; i += 4) {
		int32_t f = i * 3;
		float *src_samples = src->pSamples + f;
		__m128 A0_B0_C0_A1 = _mm_loadu_ps(src_samples);
		src_samples += 4;
		__m128 B1_C1_A2_B2 = _mm_loadu_ps(src_samples);
		src_samples += 4;
		__m128 C2_A3_B3_C3 = _mm_loadu_ps(src_samples);
#if 1
		// Slightly better performance vs sse, and WAY easier to understand
		__m128 A0_A1______ = _mm_shuffle_ps(A0_B0_C0_A1, A0_B0_C0_A1, _MM_SHUFFLER(0, 3, 0, 0));
		__m128 A0_A1_A2___ = _mm_insert_ps(A0_A1______, B1_C1_A2_B2, _MM_INSERT(2, 2, 0));
		__m128 A0_A1_A2_A3 = _mm_insert_ps(A0_A1_A2___, C2_A3_B3_C3, _MM_INSERT(3, 1, 0));
		__m128 ___B1_B2___ = _mm_shuffle_ps(B1_C1_A2_B2, B1_C1_A2_B2, _MM_SHUFFLER(0, 0, 3, 0));
		__m128 B0_B1_B2___ = _mm_insert_ps(___B1_B2___, A0_B0_C0_A1, _MM_INSERT(0, 1, 0));
		__m128 B0_B1_B2_B3 = _mm_insert_ps(B0_B1_B2___, C2_A3_B3_C3, _MM_INSERT(3, 2, 0));
		__m128 ______C2_C3 = _mm_shuffle_ps(C2_A3_B3_C3, C2_A3_B3_C3, _MM_SHUFFLER(0, 0, 0, 3));
		__m128 C0____C2_C3 = _mm_insert_ps(______C2_C3, A0_B0_C0_A1, _MM_INSERT(0, 2, 0));
		__m128 C0_C1_C2_C3 = _mm_insert_ps(C0____C2_C3, B1_C1_A2_B2, _MM_INSERT(1, 1, 0));
#else
		// This performance is within error of sse
		__m128 A0____A2_A1 = _mm_blend_ps(A0_B0_C0_A1, B1_C1_A2_B2, 0b0100);
		__m128 A0_A3_A2_A1 = _mm_blend_ps(A0____A2_A1, C2_A3_B3_C3, 0b0010);
		__m128 A0_A1_A2_A3 = _mm_shuffle_ps(A0_A3_A2_A1, A0_A3_A2_A1, _MM_SHUFFLER(0, 3, 2, 1));
		__m128 B1_B0____B2 = _mm_blend_ps(A0_B0_C0_A1, B1_C1_A2_B2, 0b1001);
		__m128 B1_B0_B3_B2 = _mm_blend_ps(B1_B0____B2, C2_A3_B3_C3, 0b0100);
		__m128 B0_B1_B2_B3 = _mm_shuffle_ps(B1_B0_B3_B2, B1_B0_B3_B2, _MM_SHUFFLER(1, 0, 3, 2));
		__m128 ___C1_C0___ = _mm_blend_ps(A0_B0_C0_A1, B1_C1_A2_B2, 0b0010);
		__m128 C2_C1_C0_C3 = _mm_blend_ps(___C1_C0___, C2_A3_B3_C3, 0b1001);
		__m128 C0_C1_C2_C3 = _mm_shuffle_ps(C2_C1_C0_C3, C2_C1_C0_C3, _MM_SHUFFLER(2, 1, 0, 3));
#endif
		_mm_storeu_ps(dst_samples_a + i, A0_A1_A2_A3);
		_mm_storeu_ps(dst_samples_b + i, B0_B1_B2_B3);
		_mm_storeu_ps(dst_samples_c + i, C0_C1_C2_C3);
	}
	for (int32_t f = i * 3; i < (int32_t)src->frames; i++) {
		dst_samples_a[i] = src->pSamples[f++];
		dst_samples_b[i] = src->pSamples[f++];
		dst_samples_c[i] = src->pSamples[f++];
	}
}

AZA_SIMD_FEATURES("avx")
void azaBufferDeinterlace3Ch_avx(azaBuffer *dst, azaBuffer *src) {
	int32_t i = 0;
	float *const dst_samples_a = dst->pSamples;
	float *const dst_samples_b = dst_samples_a + dst->frames;
	float *const dst_samples_c = dst_samples_b + dst->frames;
	for (; i <= (int32_t)src->frames-8; i += 8) {
		int32_t f = i * 3;
		float *src_samples = src->pSamples + f;
		__m256 A0_B0_C0_A1_B1_C1_A2_B2 = _mm256_loadu_ps(src_samples);
		src_samples += 8;
		__m256 C2_A3_B3_C3_A4_B4_C4_A5 = _mm256_loadu_ps(src_samples);
		src_samples += 8;
		__m256 B5_C5_A6_B6_C6_A7_B7_C7 = _mm256_loadu_ps(src_samples);

		__m256 A0_B0_C0_A1_A4_B4_C4_A5 = _mm256_permute2f128_ps(
			A0_B0_C0_A1_B1_C1_A2_B2, C2_A3_B3_C3_A4_B4_C4_A5, _MM256_PERMUTE2F128(0, 3)
		);
		__m256 B1_C1_A2_B2_B5_C5_A6_B6 = _mm256_permute2f128_ps(
			A0_B0_C0_A1_B1_C1_A2_B2, B5_C5_A6_B6_C6_A7_B7_C7, _MM256_PERMUTE2F128(1, 2)
		);
		__m256 C2_A3_B3_C3_C6_A7_B7_C7 = _mm256_permute2f128_ps(
			C2_A3_B3_C3_A4_B4_C4_A5, B5_C5_A6_B6_C6_A7_B7_C7, _MM256_PERMUTE2F128(0, 3)
		);

		__m256 ___A3_A2_______A7_A6___ = _mm256_blend_ps(
			B1_C1_A2_B2_B5_C5_A6_B6, C2_A3_B3_C3_C6_A7_B7_C7, 0b00100010
		);
		__m256 B1_B0____B2_B5_B4____B6 = _mm256_blend_ps(
			A0_B0_C0_A1_A4_B4_C4_A5, B1_C1_A2_B2_B5_C5_A6_B6, 0b10011001
		);
		__m256 B1_B0_B3_B2_B5_B4_B7_B6 = _mm256_blend_ps(
			B1_B0____B2_B5_B4____B6, C2_A3_B3_C3_C6_A7_B7_C7, 0b01000100
		);
		__m256 ___C1_C0_______C5_C4___ = _mm256_blend_ps(
			A0_B0_C0_A1_A4_B4_C4_A5, B1_C1_A2_B2_B5_C5_A6_B6, 0b00100010
		);

		__m256 A0_A1_A2_A3_A4_A5_A6_A7 = _mm256_shuffle_ps(
			A0_B0_C0_A1_A4_B4_C4_A5, ___A3_A2_______A7_A6___, _MM_SHUFFLER(0, 3, 2, 1)
		);
		__m256 B0_B1_B2_B3_B4_B5_B6_B7 = _mm256_shuffle_ps(
			B1_B0_B3_B2_B5_B4_B7_B6, B1_B0_B3_B2_B5_B4_B7_B6, _MM_SHUFFLER(1, 0, 3, 2)
		);
		__m256 C0_C1_C2_C3_C4_C5_C6_C7 = _mm256_shuffle_ps(
			___C1_C0_______C5_C4___, C2_A3_B3_C3_C6_A7_B7_C7, _MM_SHUFFLER(2, 1, 0, 3)
		);

		_mm256_storeu_ps(dst_samples_a + i, A0_A1_A2_A3_A4_A5_A6_A7);
		_mm256_storeu_ps(dst_samples_b + i, B0_B1_B2_B3_B4_B5_B6_B7);
		_mm256_storeu_ps(dst_samples_c + i, C0_C1_C2_C3_C4_C5_C6_C7);
	}
	for (int32_t f = i * 3; i < (int32_t)src->frames; i++) {
		dst_samples_a[i] = src->pSamples[f++];
		dst_samples_b[i] = src->pSamples[f++];
		dst_samples_c[i] = src->pSamples[f++];
	}
}

// TODO: Delete this, since the above gets the same or better performance without needing AVX2
AZA_SIMD_FEATURES("avx2")
void azaBufferDeinterlace3Ch_avx2(azaBuffer *dst, azaBuffer *src) {
	int32_t i = 0;
	float *const dst_samples_a = dst->pSamples;
	float *const dst_samples_b = dst->pSamples + dst->frames;
	float *const dst_samples_c = dst->pSamples + (dst->frames<<1);
	for (; i <= (int32_t)src->frames-8; i += 8) {
		int32_t f = i * 3;
		float *src_samples = src->pSamples + f;
		__m256 A0_B0_C0_A1_B1_C1_A2_B2 = _mm256_loadu_ps(src_samples);
		src_samples += 8;
		__m256 C2_A3_B3_C3_A4_B4_C4_A5 = _mm256_loadu_ps(src_samples);
		src_samples += 8;
		__m256 B5_C5_A6_B6_C6_A7_B7_C7 = _mm256_loadu_ps(src_samples);

		__m256 A0_A3____A1_A4____A2_A5 = _mm256_blend_ps(
			A0_B0_C0_A1_B1_C1_A2_B2, C2_A3_B3_C3_A4_B4_C4_A5, 0b10010010
		);
		__m256 A0_A3_A6_A1_A4_A7_A2_A5 = _mm256_blend_ps(
			A0_A3____A1_A4____A2_A5, B5_C5_A6_B6_C6_A7_B7_C7, 0b00100100
		);
		__m256 A0_A1_A2_A3_A4_A5_A6_A7 = _mm256_permutevar8x32_ps(
			A0_A3_A6_A1_A4_A7_A2_A5, _mm256_setr_epi32(0, 3, 6, 1, 4, 7, 2, 5)
		);
		__m256 ___B0_B3____B1_B4____B2 = _mm256_blend_ps(
			A0_B0_C0_A1_B1_C1_A2_B2, C2_A3_B3_C3_A4_B4_C4_A5, 0b00100100
		);
		__m256 B5_B0_B3_B6_B1_B4_B7_B2 = _mm256_blend_ps(
			___B0_B3____B1_B4____B2, B5_C5_A6_B6_C6_A7_B7_C7, 0b01001001
		);
		__m256 B0_B1_B2_B3_B4_B5_B6_B7 = _mm256_permutevar8x32_ps(
			B5_B0_B3_B6_B1_B4_B7_B2, _mm256_setr_epi32(1, 4, 7, 2, 5, 0, 3, 6)
		);
		__m256 C2____C0_C3____C1_C4___ = _mm256_blend_ps(
			A0_B0_C0_A1_B1_C1_A2_B2, C2_A3_B3_C3_A4_B4_C4_A5, 0b01001001
		);
		__m256 C2_C5_C0_C3_C6_C1_C4_C7 = _mm256_blend_ps(
			C2____C0_C3____C1_C4___, B5_C5_A6_B6_C6_A7_B7_C7, 0b10010010
		);
		__m256 C0_C1_C2_C3_C4_C5_C6_C7 = _mm256_permutevar8x32_ps(
			C2_C5_C0_C3_C6_C1_C4_C7, _mm256_setr_epi32(2, 5, 0, 3, 6, 1, 4, 7)
		);

		_mm256_storeu_ps(dst_samples_a + i, A0_A1_A2_A3_A4_A5_A6_A7);
		_mm256_storeu_ps(dst_samples_b + i, B0_B1_B2_B3_B4_B5_B6_B7);
		_mm256_storeu_ps(dst_samples_c + i, C0_C1_C2_C3_C4_C5_C6_C7);
	}
	for (int32_t f = i * 3; i < (int32_t)src->frames; i++) {
		dst_samples_a[i] = src->pSamples[f++];
		dst_samples_b[i] = src->pSamples[f++];
		dst_samples_c[i] = src->pSamples[f++];
	}
}

void azaBufferDeinterlace3Ch_dispatch(azaBuffer*, azaBuffer*);
void (*azaBufferDeinterlace3Ch)(azaBuffer *dst, azaBuffer *src) = azaBufferDeinterlace3Ch_dispatch;
void azaBufferDeinterlace3Ch_dispatch(azaBuffer *dst, azaBuffer *src) {
	assert(azaCPUID.initted);
	if (AZA_AVX) {
		azaBufferDeinterlace3Ch = azaBufferDeinterlace3Ch_avx;
	} else if (AZA_SSE4_1) {
		azaBufferDeinterlace3Ch = azaBufferDeinterlace3Ch_sse4_1;
	} else if (AZA_SSE) {
		azaBufferDeinterlace3Ch = azaBufferDeinterlace3Ch_sse;
	} else {
		azaBufferDeinterlace3Ch = azaBufferDeinterlace3Ch_scalar;
	}
	azaBufferDeinterlace3Ch(dst, src);
}

// 4 channel specialized deinterlace

void azaBufferDeinterlace4Ch_scalar(azaBuffer *dst, azaBuffer *src) {
	float *const dst_samples_a = dst->pSamples;
	float *const dst_samples_b = dst_samples_a + dst->frames;
	float *const dst_samples_c = dst_samples_b + dst->frames;
	float *const dst_samples_d = dst_samples_c + dst->frames;
	for (uint32_t i = 0, f = 0; i < dst->frames; i++) {
		dst_samples_a[i] = src->pSamples[f++];
		dst_samples_b[i] = src->pSamples[f++];
		dst_samples_c[i] = src->pSamples[f++];
		dst_samples_d[i] = src->pSamples[f++];
	}
}

AZA_SIMD_FEATURES("sse")
void azaBufferDeinterlace4Ch_sse(azaBuffer *dst, azaBuffer *src) {
	int32_t i = 0;
	float *const dst_samples_a = dst->pSamples;
	float *const dst_samples_b = dst_samples_a + dst->frames;
	float *const dst_samples_c = dst_samples_b + dst->frames;
	float *const dst_samples_d = dst_samples_c + dst->frames;
	for (; i <= (int32_t)src->frames-4; i += 4) {
		int32_t f = i << 2;
		float *src_samples = src->pSamples + f;
		__m128 A0_B0_C0_D0 = _mm_loadu_ps(src_samples);
		src_samples += 4;
		__m128 A1_B1_C1_D1 = _mm_loadu_ps(src_samples);
		src_samples += 4;
		__m128 A2_B2_C2_D2 = _mm_loadu_ps(src_samples);
		src_samples += 4;
		__m128 A3_B3_C3_D3 = _mm_loadu_ps(src_samples);

		__m128 A0_A1_B0_B1 = _mm_unpacklo_ps(A0_B0_C0_D0, A1_B1_C1_D1);
		__m128 A2_A3_B2_B3 = _mm_unpacklo_ps(A2_B2_C2_D2, A3_B3_C3_D3);
		__m128 C0_C1_D0_D1 = _mm_unpackhi_ps(A0_B0_C0_D0, A1_B1_C1_D1);
		__m128 C2_C3_D2_D3 = _mm_unpackhi_ps(A2_B2_C2_D2, A3_B3_C3_D3);

#if 1
		// These two are functionally equivalent, and on newer hardware the performance might SLIGHTLY favor the shufps one, but older hardware (such as hardware lacking AVX) favors movlhps and movhlps WRT throughput.
		__m128 A0_A1_A2_A3 = _mm_movelh_ps(A0_A1_B0_B1, A2_A3_B2_B3);
		__m128 B0_B1_B2_B3 = _mm_movehl_ps(A2_A3_B2_B3, A0_A1_B0_B1);
		__m128 C0_C1_C2_C3 = _mm_movelh_ps(C0_C1_D0_D1, C2_C3_D2_D3);
		__m128 D0_D1_D2_D3 = _mm_movehl_ps(C2_C3_D2_D3, C0_C1_D0_D1);
#else
		__m128 A0_A1_A2_A3 = _mm_shuffle_ps(A0_A1_B0_B1, A2_A3_B2_B3, _MM_SHUFFLER(0, 1, 0, 1));
		__m128 B0_B1_B2_B3 = _mm_shuffle_ps(A0_A1_B0_B1, A2_A3_B2_B3, _MM_SHUFFLER(2, 3, 2, 3));
		__m128 C0_C1_C2_C3 = _mm_shuffle_ps(C0_C1_D0_D1, C2_C3_D2_D3, _MM_SHUFFLER(0, 1, 0, 1));
		__m128 D0_D1_D2_D3 = _mm_shuffle_ps(C0_C1_D0_D1, C2_C3_D2_D3, _MM_SHUFFLER(2, 3, 2, 3));
#endif
		_mm_storeu_ps(dst_samples_a + i, A0_A1_A2_A3);
		_mm_storeu_ps(dst_samples_b + i, B0_B1_B2_B3);
		_mm_storeu_ps(dst_samples_c + i, C0_C1_C2_C3);
		_mm_storeu_ps(dst_samples_d + i, D0_D1_D2_D3);
	}
	for (int32_t f = i << 2; i < (int32_t)src->frames; i++) {
		dst_samples_a[i] = src->pSamples[f++];
		dst_samples_b[i] = src->pSamples[f++];
		dst_samples_c[i] = src->pSamples[f++];
		dst_samples_d[i] = src->pSamples[f++];
	}
}

AZA_SIMD_FEATURES("avx")
void azaBufferDeinterlace4Ch_avx(azaBuffer *dst, azaBuffer *src) {
	int32_t i = 0;
	float *const dst_samples_a = dst->pSamples;
	float *const dst_samples_b = dst_samples_a + dst->frames;
	float *const dst_samples_c = dst_samples_b + dst->frames;
	float *const dst_samples_d = dst_samples_c + dst->frames;
	for (; i <= (int32_t)src->frames-8; i += 8) {
		int32_t f = i << 2;
		float *src_samples = src->pSamples + f;
		__m256 A0_B0_C0_D0_A1_B1_C1_D1 = _mm256_loadu_ps(src_samples);
		src_samples += 8;
		__m256 A2_B2_C2_D2_A3_B3_C3_D3 = _mm256_loadu_ps(src_samples);
		src_samples += 8;
		__m256 A4_B4_C4_D4_A5_B5_C5_D5 = _mm256_loadu_ps(src_samples);
		src_samples += 8;
		__m256 A6_B6_C6_D6_A7_B7_C7_D7 = _mm256_loadu_ps(src_samples);

		__m256 A0_B0_C0_D0_A4_B4_C4_D4 = _mm256_permute2f128_ps(
			A0_B0_C0_D0_A1_B1_C1_D1, A4_B4_C4_D4_A5_B5_C5_D5, _MM256_PERMUTE2F128(0, 2)
		);
		__m256 A1_B1_C1_D1_A5_B5_C5_D5 = _mm256_permute2f128_ps(
			A0_B0_C0_D0_A1_B1_C1_D1, A4_B4_C4_D4_A5_B5_C5_D5, _MM256_PERMUTE2F128(1, 3)
		);
		__m256 A2_B2_C2_D2_A6_B6_C6_D6 = _mm256_permute2f128_ps(
			A2_B2_C2_D2_A3_B3_C3_D3, A6_B6_C6_D6_A7_B7_C7_D7, _MM256_PERMUTE2F128(0, 2)
		);
		__m256 A3_B3_C3_D3_A7_B7_C7_D7 = _mm256_permute2f128_ps(
			A2_B2_C2_D2_A3_B3_C3_D3, A6_B6_C6_D6_A7_B7_C7_D7, _MM256_PERMUTE2F128(1, 3)
		);

		__m256 A0_A1_B0_B1_A4_A5_B4_B5 = _mm256_unpacklo_ps(A0_B0_C0_D0_A4_B4_C4_D4, A1_B1_C1_D1_A5_B5_C5_D5);
		__m256 C0_C1_D0_D1_C4_C5_D4_D5 = _mm256_unpackhi_ps(A0_B0_C0_D0_A4_B4_C4_D4, A1_B1_C1_D1_A5_B5_C5_D5);
		__m256 A2_A3_B2_B3_A6_A7_B6_B7 = _mm256_unpacklo_ps(A2_B2_C2_D2_A6_B6_C6_D6, A3_B3_C3_D3_A7_B7_C7_D7);
		__m256 C2_C3_D2_D3_C6_C7_D6_D7 = _mm256_unpackhi_ps(A2_B2_C2_D2_A6_B6_C6_D6, A3_B3_C3_D3_A7_B7_C7_D7);

		__m256 A0_A1_A2_A3_A4_A5_A6_A7 = _mm256_shuffle_ps(
			A0_A1_B0_B1_A4_A5_B4_B5, A2_A3_B2_B3_A6_A7_B6_B7, _MM_SHUFFLER(0, 1, 0, 1)
		);
		__m256 B0_B1_B2_B3_B4_B5_B6_B7 = _mm256_shuffle_ps(
			A0_A1_B0_B1_A4_A5_B4_B5, A2_A3_B2_B3_A6_A7_B6_B7, _MM_SHUFFLER(2, 3, 2, 3)
		);
		__m256 C0_C1_C2_C3_C4_C5_C6_C7 = _mm256_shuffle_ps(
			C0_C1_D0_D1_C4_C5_D4_D5, C2_C3_D2_D3_C6_C7_D6_D7, _MM_SHUFFLER(0, 1, 0, 1)
		);
		__m256 D0_D1_D2_D3_D4_D5_D6_D7 = _mm256_shuffle_ps(
			C0_C1_D0_D1_C4_C5_D4_D5, C2_C3_D2_D3_C6_C7_D6_D7, _MM_SHUFFLER(2, 3, 2, 3)
		);

		_mm256_storeu_ps(dst_samples_a + i, A0_A1_A2_A3_A4_A5_A6_A7);
		_mm256_storeu_ps(dst_samples_b + i, B0_B1_B2_B3_B4_B5_B6_B7);
		_mm256_storeu_ps(dst_samples_c + i, C0_C1_C2_C3_C4_C5_C6_C7);
		_mm256_storeu_ps(dst_samples_d + i, D0_D1_D2_D3_D4_D5_D6_D7);
	}
	for (int32_t f = i << 2; i < (int32_t)src->frames; i++) {
		dst_samples_a[i] = src->pSamples[f++];
		dst_samples_b[i] = src->pSamples[f++];
		dst_samples_c[i] = src->pSamples[f++];
		dst_samples_d[i] = src->pSamples[f++];
	}
}

// TODO: delete this, since the avx one is simpler and faster
AZA_SIMD_FEATURES("avx2")
void azaBufferDeinterlace4Ch_avx2(azaBuffer *dst, azaBuffer *src) {
	int32_t i = 0;
	float *const dst_samples_a = dst->pSamples;
	float *const dst_samples_b = dst_samples_a + dst->frames;
	float *const dst_samples_c = dst_samples_b + dst->frames;
	float *const dst_samples_d = dst_samples_c + dst->frames;
	for (; i <= (int32_t)src->frames-8; i += 8) {
		int32_t f = i << 2;
		float *src_samples = src->pSamples + f;
		__m256 A0_B0_C0_D0_A1_B1_C1_D1 = _mm256_loadu_ps(src_samples);
		src_samples += 8;
		__m256 A2_B2_C2_D2_A3_B3_C3_D3 = _mm256_loadu_ps(src_samples);
		src_samples += 8;
		__m256 A4_B4_C4_D4_A5_B5_C5_D5 = _mm256_loadu_ps(src_samples);
		src_samples += 8;
		__m256 A6_B6_C6_D6_A7_B7_C7_D7 = _mm256_loadu_ps(src_samples);

		// This is about 15% faster than sse
		__m256 A0_A1_B0_B1_C0_C1_D0_D1 = _mm256_permutevar8x32_ps(
			A0_B0_C0_D0_A1_B1_C1_D1, _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7)
		);
		__m256 A2_A3_B2_B3_C2_C3_D2_D3 = _mm256_permutevar8x32_ps(
			A2_B2_C2_D2_A3_B3_C3_D3, _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7)
		);
		__m256 A4_A5_B4_B5_C4_C5_D4_D5 = _mm256_permutevar8x32_ps(
			A4_B4_C4_D4_A5_B5_C5_D5, _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7)
		);
		__m256 A6_A7_B6_B7_C6_C7_D6_D7 = _mm256_permutevar8x32_ps(
			A6_B6_C6_D6_A7_B7_C7_D7, _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7)
		);

		__m256 ______A2_A3_______C2_C3 = _mm256_shuffle_ps(
			A2_A3_B2_B3_C2_C3_D2_D3, A2_A3_B2_B3_C2_C3_D2_D3, _MM_SHUFFLER(0, 0, 0, 1)
		);
		__m256 A0_A1_A2_A3_C0_C1_C2_C3 = _mm256_blend_ps(
			A0_A1_B0_B1_C0_C1_D0_D1, ______A2_A3_______C2_C3, 0b11001100
		);
		__m256 B0_B1_______D0_D1______ = _mm256_shuffle_ps(
			A0_A1_B0_B1_C0_C1_D0_D1, A0_A1_B0_B1_C0_C1_D0_D1, _MM_SHUFFLER(2, 3, 0, 0)
		);
		__m256 B0_B1_B2_B3_D0_D1_D2_D3 = _mm256_blend_ps(
			B0_B1_______D0_D1______, A2_A3_B2_B3_C2_C3_D2_D3, 0b11001100
		);

		__m256 ______A6_A7_______C6_C7 = _mm256_shuffle_ps(
			A6_A7_B6_B7_C6_C7_D6_D7, A6_A7_B6_B7_C6_C7_D6_D7, _MM_SHUFFLER(0, 0, 0, 1)
		);
		__m256 A4_A5_A6_A7_C4_C5_C6_C7 = _mm256_blend_ps(
			A4_A5_B4_B5_C4_C5_D4_D5, ______A6_A7_______C6_C7, 0b11001100
		);
		__m256 B4_B5_______D4_D5______ = _mm256_shuffle_ps(
			A4_A5_B4_B5_C4_C5_D4_D5, A4_A5_B4_B5_C4_C5_D4_D5, _MM_SHUFFLER(2, 3, 0, 0)
		);
		__m256 B4_B5_B6_B7_D4_D5_D6_D7 = _mm256_blend_ps(
			B4_B5_______D4_D5______, A6_A7_B6_B7_C6_C7_D6_D7, 0b11001100
		);
		__m256 A0_A1_A2_A3_A4_A5_A6_A7 = _mm256_permute2f128_ps(
			A0_A1_A2_A3_C0_C1_C2_C3, A4_A5_A6_A7_C4_C5_C6_C7, _MM256_PERMUTE2F128(0, 2)
		);
		__m256 B0_B1_B2_B3_B4_B5_B6_B7 = _mm256_permute2f128_ps(
			B0_B1_B2_B3_D0_D1_D2_D3, B4_B5_B6_B7_D4_D5_D6_D7, _MM256_PERMUTE2F128(0, 2)
		);
		__m256 C0_C1_C2_C3_C4_C5_C6_C7 = _mm256_permute2f128_ps(
			A0_A1_A2_A3_C0_C1_C2_C3, A4_A5_A6_A7_C4_C5_C6_C7, _MM256_PERMUTE2F128(1, 3)
		);
		__m256 D0_D1_D2_D3_D4_D5_D6_D7 = _mm256_permute2f128_ps(
			B0_B1_B2_B3_D0_D1_D2_D3, B4_B5_B6_B7_D4_D5_D6_D7, _MM256_PERMUTE2F128(1, 3)
		);

		_mm256_storeu_ps(dst_samples_a + i, A0_A1_A2_A3_A4_A5_A6_A7);
		_mm256_storeu_ps(dst_samples_b + i, B0_B1_B2_B3_B4_B5_B6_B7);
		_mm256_storeu_ps(dst_samples_c + i, C0_C1_C2_C3_C4_C5_C6_C7);
		_mm256_storeu_ps(dst_samples_d + i, D0_D1_D2_D3_D4_D5_D6_D7);
	}
	for (int32_t f = i << 2; i < (int32_t)src->frames; i++) {
		dst_samples_a[i] = src->pSamples[f++];
		dst_samples_b[i] = src->pSamples[f++];
		dst_samples_c[i] = src->pSamples[f++];
		dst_samples_d[i] = src->pSamples[f++];
	}
}

void azaBufferDeinterlace4Ch_dispatch(azaBuffer*, azaBuffer*);
void (*azaBufferDeinterlace4Ch)(azaBuffer *dst, azaBuffer *src) = azaBufferDeinterlace4Ch_dispatch;
void azaBufferDeinterlace4Ch_dispatch(azaBuffer *dst, azaBuffer *src) {
	assert(azaCPUID.initted);
	if (AZA_AVX) {
		azaBufferDeinterlace4Ch = azaBufferDeinterlace4Ch_avx;
	} else if (AZA_SSE) {
		azaBufferDeinterlace4Ch = azaBufferDeinterlace4Ch_sse;
	} else {
		azaBufferDeinterlace4Ch = azaBufferDeinterlace4Ch_scalar;
	}
	azaBufferDeinterlace4Ch(dst, src);
}

// Specialization dispatch

void azaBufferDeinterlace(azaBuffer *dst, azaBuffer *src) {
	assert(dst->frames == src->frames);
	// We don't actually have to worry about dst->stride because we just kinda ignore it anyway
	// assert(dst->stride == dst->channelLayout.count);
	assert(dst->channelLayout.count == src->channelLayout.count);
	if (src->stride == src->channelLayout.count) {
		switch (dst->channelLayout.count) {
			case 0:
			case 1:
				azaBufferCopy(dst, src);
				return;
			case 2:
				azaBufferDeinterlace2Ch(dst, src);
				return;
			case 3:
				azaBufferDeinterlace3Ch(dst, src);
				return;
			case 4:
				azaBufferDeinterlace4Ch(dst, src);
				return;
			// TODO: Specialized implementations up to 8 channels would be nice, as 7.1 is fairly common. Beyond that, maybe just non-dynamic scalar implementations would still be good? The dynamic fallback is pretty bad...
			// We might also be able to benefit from GCC's target_clones, but MSVC doesn't have that. To get the same kinda auto-vectorization available in MSVC would require different compilation units for each target, and again manual dispatch.
			default:
				azaBufferDeinterlace_dynamic(dst, src);
				return;
		}
	}
	if (dst->channelLayout.count <= 1) {
		azaBufferCopy(dst, src);
	} else {
		azaBufferDeinterlace_dynamic(dst, src);
	}
}