/*
	File: simd.h
	Author: Philip Haynes
	Provides some utility on top of simd primitives
	BIG TODO: When ARM testing becomes feasible, handle NEON stuff as well.

	Some useful facts:
	- x86_64 has SSE and SSE2 as core feature sets (guaranteed to be available)
*/

#ifndef AZAUDIO_SIMD_H
#define AZAUDIO_SIMD_H

#include "cpuid.h"

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

#define _MM_SHUFFLER(w, x, y, z) _MM_SHUFFLE(z, y, x, w)

#define _MM_INSERT(dst, src, clear) (((src) << 6) | ((dst) << 4) | (clear))

#define _MM256_PERMUTE2F128(src0, src1) (((src1) << 4) | (src0))

#if defined(_MSC_VER)
	// MSVC lets you use intrinsics for any SIMD level regardless of compiler flags, so this doesn't have to do anything
	#define AZA_SIMD_FEATURES(...)
#elif defined(__GNUC__) || defined(__clang__)
	#define AZA_SIMD_FEATURES(...) __attribute__((target(__VA_ARGS__)))

	// I would love to take advantage of ifunc with gcc but for whatever reason, it's impossible to use hand-rolled specialized functions with it in C mode. C++ only feature apparently :/
	// At the very least this slightly simplifies the code, so we don't have to directly support 2 different dispatch models. We're doing almost the same thing as ifunc manually anyway, just slightly worse (probably).
#else

#warning "platform not supported"

#endif

#define AZA_AVX2 azaCPUID.avx2
#define AZA_FMA azaCPUID.fma
#define AZA_AVX azaCPUID.avx
#define AZA_SSE4_2 azaCPUID.sse4_2
#define AZA_SSE4_1 azaCPUID.sse4_1
#define AZA_SSSE3 azaCPUID.ssse3
#define AZA_SSE3 azaCPUID.sse3
#define AZA_SSE2 azaCPUID.sse2
#define AZA_SSE azaCPUID.sse

// Union for accessing specific scalars in a float vector
// NOTE: If you have to use this, you're probably not really gaining any performance by using SIMD
typedef union float_x8 {
	__m256 v;
	float f[8];
} float_x8;

AZA_SIMD_FEATURES("sse,fma")
static AZA_FORCE_INLINE(float)
aza_fmadd_f32(float a, float b, float c) {
	__m128 result = _mm_fmadd_ss(_mm_set_ss(a), _mm_set_ss(b), _mm_set_ss(c));
	return _mm_cvtss_f32(result);
}

// returns the scalar sum of all lanes
AZA_SIMD_FEATURES("sse3")
static inline float
aza_mm_hsum_ps_sse3(__m128 a) {
	// Using _mm_movehdup_ps instead of _mm_shuffle_ps gets rid of a vmovaps, and is why this section is SSE3
	//                                      a = 0, 1, 2, 3
	__m128 shuf = _mm_movehdup_ps(a); // shuf = 1,   1,   3,   3
	__m128 sums = _mm_add_ps(a, shuf);// sums = 0+1, 1+1, 2+3, 3+3
	shuf = _mm_movehl_ps(shuf, sums); // shuf = 2+3, 3+3, 3,   3
	sums = _mm_add_ss(sums, shuf);    // sums = 0+1+2+3, 1+1, 2+3, 3+3
	return _mm_cvtss_f32(sums);       // return 0+1+2+3
}

// returns the scalar sum of all lanes
AZA_SIMD_FEATURES("sse")
static inline float
aza_mm_hsum_ps_sse(__m128 a) {
	//                                                                a = 0, 1, 2, 3
	__m128 shuf = _mm_shuffle_ps(a, a, _MM_SHUFFLE(2, 3, 0, 1));// shuf = 1, 0, 3, 2
	__m128 sums = _mm_add_ps(shuf, a);                          // sums = 0+1, 0+1, 2+3, 2+3
	shuf = _mm_movehl_ps(shuf, sums);                           // shuf = 2+3, 2+3, 3, 2
	sums = _mm_add_ss(sums, shuf);                              // shuf = 0+1+2+3, 0+1, 2+3, 2+3
	return _mm_cvtss_f32(shuf);                                 // return 0+1+2+3
}

// returns the scalar sum of all lanes
AZA_SIMD_FEATURES("avx")
static inline float
aza_mm256_hsum_ps(__m256 a) {
	__m128 lo, hi;
	lo = _mm256_extractf128_ps(a, 0);
	hi = _mm256_extractf128_ps(a, 1);
	return aza_mm_hsum_ps_sse3(_mm_add_ps(lo, hi));
}

AZA_SIMD_FEATURES("avx,fma")
static AZA_FORCE_INLINE(__m256)
azaLerp_x8_avx_fma(__m256 a, __m256 b, __m256 t) {
	__m256 result = _mm256_fmadd_ps(_mm256_sub_ps(b, a), t, a);
	return result;
}

AZA_SIMD_FEATURES("avx")
static AZA_FORCE_INLINE(__m256)
azaLerp_x8_avx(__m256 a, __m256 b, __m256 t) {
	__m256 result = _mm256_add_ps(_mm256_mul_ps(_mm256_sub_ps(b, a), t), a);
	return result;
}

#endif // AZAUDIO_SIMD_H