/*
	File: azaBufferMixMatrix.c
	Author: Philip Haynes
	Specialized implementations of azaBufferMixMatrix and dispatch.
	azaBufferMixMatrix(5) is declared in dsp.h
*/

#include "../dsp/azaBuffer.h"
#include "../simd.h"

// Dynamic fallback

void azaBufferMixMatrix_scalar(azaBuffer *dst, float volumeDst, azaBuffer *src, float volumeSrc, azaChannelMatrix *matrix) {
	// src channel count columns, dst channel count rows
	float *matPremult = (float*)alloca(dst->channelLayout.count * src->channelLayout.count * sizeof(float));
	for (uint8_t c = 0; c < src->channelLayout.count; c++) {
		for (uint8_t r = 0; r < dst->channelLayout.count; r++) {
			matPremult[r * src->channelLayout.count + c] = volumeSrc * matrix->matrix[c * matrix->outputs + r];
		}
	}
	uint32_t i = 0;
	uint32_t totalSamples = dst->frames*dst->channelLayout.count;
	for (; i < totalSamples; i++) {
		uint32_t dstFrame = i / dst->channelLayout.count;
		uint32_t dstC = i % dst->channelLayout.count;
		float accum = 0.0f;
		float *row = matPremult + src->channelLayout.count * dstC;
		for (uint32_t srcC = 0; srcC < src->channelLayout.count; srcC++) {
			accum += src->pSamples[dstFrame * src->stride + srcC] * row[srcC];
		}
		dst->pSamples[dstFrame * dst->stride + dstC] = dst->pSamples[dstFrame * dst->stride + dstC] * volumeDst + accum;
	}
}

AZA_SIMD_FEATURES("avx,fma")
void azaBufferMixMatrix_avx_fma(azaBuffer *dst, float volumeDst, azaBuffer *src, float volumeSrc, azaChannelMatrix *matrix) {
	// src channel count columns, dst channel count rows
	float *matPremult = (float*)alloca(dst->channelLayout.count * src->channelLayout.count * sizeof(float));
	for (uint8_t c = 0; c < src->channelLayout.count; c++) {
		for (uint8_t r = 0; r < dst->channelLayout.count; r++) {
			matPremult[r * src->channelLayout.count + c] = volumeSrc * matrix->matrix[c * matrix->outputs + r];
		}
	}
	uint32_t i = 0;
	uint32_t totalSamples = dst->frames*dst->channelLayout.count;
	if ((uint64_t)dst->pSamples % 32 != 0) {
		// Process the first few as scalar to get into the right alignment
		// _mm256_load_ps and _mm256_store_ps expects the data to be aligned on a 32-byte boundary
		// We could instead use _mm256_loadu_ps and _mm256_storeu_ps but it's easy enough to just get into a sufficiently-aligned boundary like this. The performance differences should be minimal on modern CPUs.
		for (; i < 8 - ((uint64_t)dst->pSamples % 32) / sizeof(float); i++) {
			uint32_t dstFrame = i / dst->channelLayout.count;
			uint32_t dstC = i % dst->channelLayout.count;
			float accum = 0.0f;
			float *row = matPremult + src->channelLayout.count * dstC;
			for (uint32_t srcC = 0; srcC < src->channelLayout.count; srcC++) {
				accum += src->pSamples[dstFrame * src->stride + srcC] * row[srcC];
			}
			dst->pSamples[dstFrame * dst->stride + dstC] = dst->pSamples[dstFrame * dst->stride + dstC] * volumeDst + accum;
		}
	}
	__m256 volumeDst_x8 = _mm256_set1_ps(volumeDst);
	for (; i <= totalSamples-8; i += 8) {
		uint32_t dstFrame = i / dst->channelLayout.count;
		uint32_t dstC = i % dst->channelLayout.count;
		__m256 accum_x8 = _mm256_setzero_ps();
		// TODO: Special cases for when the channels are better behaved
		uint32_t srcFrameOffsets[8];
		uint32_t dstCOffsets[8];
		for (uint32_t j = 0; j < 8; j++) {
			uint32_t frame = dstFrame + (j+dstC)/dst->channelLayout.count;
			srcFrameOffsets[j] = frame * src->stride;
			dstCOffsets[j] = ((j+dstC)%dst->channelLayout.count) * src->channelLayout.count;
		}
		for (uint32_t srcC = 0; srcC < src->channelLayout.count; srcC++) {
			// TODO: This sucks ass
			__m256 srcSamples_x8 = _mm256_setr_ps(
				src->pSamples[srcFrameOffsets[0] + srcC],
				src->pSamples[srcFrameOffsets[1] + srcC],
				src->pSamples[srcFrameOffsets[2] + srcC],
				src->pSamples[srcFrameOffsets[3] + srcC],
				src->pSamples[srcFrameOffsets[4] + srcC],
				src->pSamples[srcFrameOffsets[5] + srcC],
				src->pSamples[srcFrameOffsets[6] + srcC],
				src->pSamples[srcFrameOffsets[7] + srcC]
			);
			__m256 mat_x8 = _mm256_setr_ps(
				matPremult[dstCOffsets[0] + srcC],
				matPremult[dstCOffsets[1] + srcC],
				matPremult[dstCOffsets[2] + srcC],
				matPremult[dstCOffsets[3] + srcC],
				matPremult[dstCOffsets[4] + srcC],
				matPremult[dstCOffsets[5] + srcC],
				matPremult[dstCOffsets[6] + srcC],
				matPremult[dstCOffsets[7] + srcC]
			);
			accum_x8 = _mm256_fmadd_ps(srcSamples_x8, mat_x8, accum_x8);
		}
		__m256 dstSamples_x8 = _mm256_load_ps(dst->pSamples + i);
		dstSamples_x8 = _mm256_fmadd_ps(dstSamples_x8, volumeDst_x8, accum_x8);
		_mm256_store_ps(dst->pSamples + i, dstSamples_x8);
	}
	for (; i < totalSamples; i++) {
		uint32_t dstFrame = i / dst->channelLayout.count;
		uint32_t dstC = i % dst->channelLayout.count;
		float accum = 0.0f;
		float *row = matPremult + src->channelLayout.count * dstC;
		for (uint32_t srcC = 0; srcC < src->channelLayout.count; srcC++) {
			accum += src->pSamples[dstFrame * src->stride + srcC] * row[srcC];
		}
		dst->pSamples[dstFrame * dst->stride + dstC] = dst->pSamples[dstFrame * dst->stride + dstC] * volumeDst + accum;
	}
}

void azaBufferMixMatrix_dispatch(azaBuffer *dst, float volumeDst, azaBuffer *src, float volumeSrc, azaChannelMatrix *matrix);
void (*azaBufferMixMatrix_general)(azaBuffer *dst, float volumeDst, azaBuffer *src, float volumeSrc, azaChannelMatrix *matrix) = azaBufferMixMatrix_dispatch;
void azaBufferMixMatrix_dispatch(azaBuffer *dst, float volumeDst, azaBuffer *src, float volumeSrc, azaChannelMatrix *matrix) {
	assert(azaCPUID.initted);
	if (AZA_AVX) {
		azaBufferMixMatrix_general = azaBufferMixMatrix_avx_fma;
	// } else if (AZA_SSE) {
	// 	azaBufferMixMatrix_general = azaBufferMixMatrix_sse;
	} else {
		azaBufferMixMatrix_general = azaBufferMixMatrix_scalar;
	}
	azaBufferMixMatrix_general(dst, volumeDst, src, volumeSrc, matrix);
}

// Specialization dispatch

void azaBufferMixMatrix(azaBuffer *dst, float volumeDst, azaBuffer *src, float volumeSrc, azaChannelMatrix *matrix) {
	assert(matrix);
	assert(matrix->inputs == src->channelLayout.count);
	assert(matrix->outputs == dst->channelLayout.count);
	assert(dst->frames == src->frames);
	if AZA_UNLIKELY(volumeDst == 1.0f && volumeSrc == 0.0f) {
		return;
	} else if AZA_UNLIKELY(volumeDst == 0.0f && volumeSrc == 0.0f) {
		azaBufferZero(dst);
	} else {
		azaBufferMixMatrix_general(dst, volumeDst, src, volumeSrc, matrix);
	}
}