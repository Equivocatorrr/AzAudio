/*
	File: math.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_MATH_H
#define AZAUDIO_MATH_H

#include <stdint.h>
#include <math.h>
#include <assert.h>

#include "header_utils.h"
#include "simd.h"

#ifdef __cplusplus
extern "C" {
#endif

static const float AZA_TAU = 6.283185307179586f;
static const float AZA_PI = 3.14159265359f;

#define AZA_DEG_TO_RAD(x) ((x) * AZA_PI / 180.0f)
#define AZA_RAD_TO_DEG(x) ((x) * 180.0f / AZA_PI)

static AZA_FORCE_INLINE(float)
azaAbsf(float a) {
	return a < 0.0f ? -a : a;
}

static AZA_FORCE_INLINE(float)
azaSqrf(float a) {
	return a * a;
}

static AZA_FORCE_INLINE(float)
azaMinf(float a, float b) {
#if __SSE__
	_mm_store_ss(&a, _mm_min_ss(_mm_set_ss(a), _mm_set_ss(b)));
#else
	a = (a < b ? a : b);
#endif
	return a;
}

static AZA_FORCE_INLINE(float)
azaMaxf(float a, float b) {
#if __SSE__
	_mm_store_ss(&a, _mm_max_ss(_mm_set_ss(a), _mm_set_ss(b)));
#else
	a = (a < b ? b : a);
#endif
	return a;
}

static AZA_FORCE_INLINE(float)
azaClampf(float a, float min, float max) {
	assert(min <= max);
	return azaMinf(azaMaxf(a, min), max);
}

static inline float
azaLinstepf(float a, float min, float max) {
	return azaClampf((a - min) / (max - min), 0.0f, 1.0f);
}

static AZA_FORCE_INLINE(float)
azaLerpf(float a, float b, float t) {
#if __SSE__
	return aza_fmadd_f32(b - a, t, a);
#else
	return (b - a) * t + a;
#endif
}

static AZA_FORCE_INLINE(float)
azaWrap01f(float a) {
	int intPart = (int)a - signbit(a);
	return a - (float)intPart;
}

float azaSincf(float x);

// sinc with a hann window with a total size of 1+2*radius
float azaLanczosf(float x, float radius);

// Like a % max except the answer is always in the range [0; max) even if the input is negative
static inline int
azaWrapi(int a, int max) {
	// assert(max > 0);
	if (a < 0) {
		return (a + 1) % max + max-1;
	} else if (a > 0) {
		return a % max;
	} else {
		return 0;
	}
}

float azaCubicf(float a, float b, float c, float d, float x);

float aza_db_to_ampf(float db);

float aza_amp_to_dbf(float amp);

static inline float
aza_ms_to_samples(float ms, float samplerate) {
	return ms * samplerate * 0.001f;
}

static inline float
aza_samples_to_ms(float samples, float samplerate) {
	return samples / samplerate * 1000.0f;
}

#define AZA_OSC_SINE_SAMPLES 128
extern float azaOscSineValues[AZA_OSC_SINE_SAMPLES+1];
// A LUT-based approximate sine oscillator where t is periodic between 0 and 1
static inline float
azaOscSine(float t) {
	t = azaWrap01f(t);
	t *= AZA_OSC_SINE_SAMPLES;
	uint32_t index = (uint32_t)t;
	float offset = t - (float)index;
	return azaLerpf(azaOscSineValues[index], azaOscSineValues[index+1], offset);
}
static AZA_FORCE_INLINE(float)
azaOscCosine(float t) {
	return azaOscSine(t + 0.25f);
}

static inline float
azaOscSquare(float t) {
	t = azaWrap01f(t);
	return (float)((int)(t * 2.0f)) * 2.0f - 1.0f;
}

static inline float
azaOscTriangle(float t) {
	return 4.0f * (azaAbsf(azaWrap01f(t + 0.25f) - 0.5f) - 0.25f);
}

static inline float
azaOscSaw(float t) {
	return azaWrap01f(t + 0.5f) * 2.0f - 1.0f;
}


typedef union azaVec3 {
	struct {
		float x, y, z;
	};
	float data[3];
} azaVec3;

static inline azaVec3 azaAddVec3(azaVec3 lhs, azaVec3 rhs) {
	return AZA_CLITERAL(azaVec3) {
		lhs.x + rhs.x,
		lhs.y + rhs.y,
		lhs.z + rhs.z,
	};
}

static inline azaVec3 azaSubVec3(azaVec3 lhs, azaVec3 rhs) {
	return AZA_CLITERAL(azaVec3) {
		lhs.x - rhs.x,
		lhs.y - rhs.y,
		lhs.z - rhs.z,
	};
}

static inline azaVec3 azaMulVec3(azaVec3 lhs, azaVec3 rhs) {
	return AZA_CLITERAL(azaVec3) {
		lhs.x * rhs.x,
		lhs.y * rhs.y,
		lhs.z * rhs.z,
	};
}

static inline azaVec3 azaDivVec3(azaVec3 lhs, azaVec3 rhs) {
	return AZA_CLITERAL(azaVec3) {
		lhs.x / rhs.x,
		lhs.y / rhs.y,
		lhs.z / rhs.z,
	};
}

static inline azaVec3 azaMulVec3Scalar(azaVec3 lhs, float rhs) {
	return AZA_CLITERAL(azaVec3) {
		lhs.x * rhs,
		lhs.y * rhs,
		lhs.z * rhs,
	};
}

static inline azaVec3 azaDivVec3Scalar(azaVec3 lhs, float rhs) {
	return AZA_CLITERAL(azaVec3) {
		lhs.x / rhs,
		lhs.y / rhs,
		lhs.z / rhs,
	};
}

static inline float azaVec3Dot(azaVec3 lhs, azaVec3 rhs) {
	return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

// Euclidian norm
static inline float azaVec3Norm(azaVec3 a) {
	return sqrtf(a.x * a.x + a.y * a.y + a.z * a.z);
}

// Euclidian norm squared
static inline float azaVec3NormSqr(azaVec3 a) {
	return a.x * a.x + a.y * a.y + a.z * a.z;
}

// Use this if the norm of a could be very small
static inline azaVec3 azaVec3NormalizedDef(azaVec3 a, float epsilon, azaVec3 def) {
	float norm = azaVec3Norm(a);
	if (norm < epsilon) {
		return def;
	}
	return azaDivVec3Scalar(a, norm);
}

static inline azaVec3 azaVec3Normalized(azaVec3 a) {
	return azaDivVec3Scalar(a, azaVec3Norm(a));
}

// 3x3 matrix with the conventions matching GLSL (same conventions as AzCore)
// - column-major memory layout
// - post-multiplication (transforms are applied in right-to-left order)
// - multiplication means lhs rows are dotted with rhs columns
// - vectors are row vectors on the lhs, and column vectors on the rhs
typedef union azaMat3 {
	struct {
		azaVec3 right, up, forward;
	};
	azaVec3 cols[3];
	float data[9];
} azaMat3;

static inline azaVec3 azaMat3Col(azaMat3 mat, uint32_t index) {
	return mat.cols[index];
}

static inline azaVec3 azaMat3Row(azaMat3 mat, uint32_t index) {
	return AZA_CLITERAL(azaVec3) {
		mat.data[index + 0],
		mat.data[index + 3],
		mat.data[index + 6],
	};
}

static inline azaVec3 azaMulMat3(azaMat3 lhs, azaMat3 rhs) {
	return AZA_CLITERAL(azaVec3) {
		lhs.data[0] * rhs.data[0] + lhs.data[3] * rhs.data[1] + lhs.data[6] * rhs.data[2],
		lhs.data[1] * rhs.data[3] + lhs.data[4] * rhs.data[4] + lhs.data[7] * rhs.data[5],
		lhs.data[2] * rhs.data[6] + lhs.data[5] * rhs.data[7] + lhs.data[8] * rhs.data[8],
	};
}

static inline azaVec3 azaMulMat3Vec3(azaMat3 lhs, azaVec3 rhs) {
	return AZA_CLITERAL(azaVec3) {
		lhs.data[0] * rhs.x + lhs.data[3] * rhs.y + lhs.data[6] * rhs.z,
		lhs.data[1] * rhs.x + lhs.data[4] * rhs.y + lhs.data[7] * rhs.z,
		lhs.data[2] * rhs.x + lhs.data[5] * rhs.y + lhs.data[8] * rhs.z,
	};
}

static inline azaVec3 azaMulVec3Mat3(azaVec3 lhs, azaMat3 rhs) {
	return AZA_CLITERAL(azaVec3) {
		azaVec3Dot(lhs, rhs.cols[0]),
		azaVec3Dot(lhs, rhs.cols[1]),
		azaVec3Dot(lhs, rhs.cols[2]),
	};
}


#ifdef __cplusplus
}
#endif
#endif // AZAUDIO_MATH_H