/*
	File: simd.h
	Author: Philip Haynes
	Provides some utility on top of simd primitives
*/

#ifndef AZAUDIO_SIMD_H
#define AZAUDIO_SIMD_H

// Deal with Michaelsoft's ineptitude yet again.
#if __AVX2__ && defined(_MSC_VER)
	#ifndef __FMA__
		#define __FMA__ 1
	#endif
#endif
#if __FMA__
	#ifndef __AVX__
		#define __AVX__ 1
	#endif
#endif
#if __AVX__
	#ifndef __SSE4_2__
		#define __SSE4_2__ 1
	#endif
#endif
#if __SSE4_2__
	#ifndef __SSE4_1__
		#define __SSE4_1__ 1
	#endif
#endif
#if __SSE4_1__
	#ifndef __SSSE3__
		#define __SSSE3__ 1
	#endif
#endif
#if __SSSE3__
	#ifndef __SSE3__
		#define __SSE3__ 1
	#endif
#endif
#if __SSE3__
	#ifndef __SSE2__
		#define __SSE2__ 1
	#endif
#endif
#if __SSE2__
	#ifndef __SSE__
		#define __SSE__ 1
	#endif
#endif

#include <immintrin.h>

#include "header_utils.h"

#if __AVX__

// Union for accessing specific scalars in a float vector
typedef union float_x8 {
	__m256 v;
	float f[8];
} float_x8;

// Does fmadd if available, else just does 2 instructions
// NOTE: This is compile-time dispatch. If you need runtime dispatch, just use the base instructions instead.
// Also note that fmadd will have very slightly different rounding behavior (round(a*b + c)) as opposed to doing a mul and add (round(round(a*b) + c)). Therefore, fmadd cannot be used where the output must match exactly across different systems (and also in null-tests against non-fmadd versions).
static AZA_FORCE_INLINE(__m256)
aza_mm256_fmadd_ps(__m256 a, __m256 b, __m256 c) {
#if __FMA__
	__m256 result = _mm256_fmadd_ps(a, b, c);
#else
	__m256 result = _mm256_add_ps(_mm256_mul_ps(a, b), c);
#endif
	return result;
}

#endif // __AVX__
#if __SSE__

// Read above note on fmadd
static AZA_FORCE_INLINE(__m128)
aza_mm_fmadd_ss(__m128 a, __m128 b, __m128 c) {
#if __FMA__
	__m128 result = _mm_fmadd_ss(a, b, c);
#else
	__m128 result = _mm_add_ss(_mm_mul_ss(a, b), c);
#endif
	return result;
}

// Read above note on fmadd
static AZA_FORCE_INLINE(float)
aza_fmadd_f32(float a, float b, float c) {
	__m128 result = aza_mm_fmadd_ss(_mm_set_ss(a), _mm_set_ss(b), _mm_set_ss(c));
	return _mm_cvtss_f32(result);
}

// returns the scalar sum of all lanes
static inline float
aza_mm_hsum_ps(__m128 a) {
#if __SSE3__                         // a = 0, 1, 2, 3
	// Using _mm_movehdup_ps instead of _mm_shuffle_ps gets rid of a vmovaps, and is why this section is SSE3
	__m128 shuf = _mm_movehdup_ps(a); // shuf = 1,   1,   3,   3
	__m128 sums = _mm_add_ps(a, shuf);// sums = 0+1, 1+1, 2+3, 3+3
	shuf = _mm_movehl_ps(shuf, sums); // shuf = 2+3, 3+3, 3,   3
	sums = _mm_add_ss(sums, shuf);    // sums = 0+1+2+3, 1+1, 2+3, 3+3
	return _mm_cvtss_f32(sums);       // return 0+1+2+3
#else // These are all SSE instructions                        // a.V = 0, 1, 2, 3
	__m128 shuf = _mm_shuffle_ps(a, a, _MM_SHUFFLE(2, 3, 0, 1));// shuf = 1, 0, 3, 2
	__m128 sums = _mm_add_ps(shuf, a);                          // sums = 0+1, 0+1, 2+3, 2+3
	shuf = _mm_movehl_ps(shuf, sums);                           // shuf = 2+3, 2+3, 3, 2
	sums = _mm_add_ss(sums, shuf);                              // shuf = 0+1+2+3, 0+1, 2+3, 2+3
	return _mm_cvtss_f32(shuf);                                 // return 0+1+2+3
#endif
}

#endif // __SSE__
#if __AVX__

// returns the scalar sum of all lanes
static inline float
aza_mm256_hsum_ps(__m256 a) {
	__m128 lo, hi;
	lo = _mm256_extractf128_ps(a, 0);
	hi = _mm256_extractf128_ps(a, 1);
	return aza_mm_hsum_ps(_mm_add_ps(lo, hi));
}

static AZA_FORCE_INLINE(__m256)
azaLerp_x8(__m256 a, __m256 b, __m256 t) {
	__m256 result = aza_mm256_fmadd_ps(_mm256_sub_ps(b, a), t, a);
	return result;
}

#endif // __AVX__

#endif // AZAUDIO_SIMD_H