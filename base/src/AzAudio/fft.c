/*
	File: fft.c
	Author: Philip Haynes
*/

#include "fft.h"

#include "math.h"

#include <assert.h>

// this is a translation of the basic implementation of the FFT from Chapter 12 of The Scientist and Engineer's Guide to Digital Signal Processing by Steven W. Smith, Ph.D.
// Some alterations have been made to make it more C-like and take advantage of bit shifts, as well as attempting to use more descriptive variable names.
// https://www.dspguide.com/ch12/3.htm

void azaFFT(float * restrict valReal, float * restrict valImag, uint32_t len) {
	assert(len > 0 && "stop it, get help");
	assert((len & (len-1)) == 0 && "len must be a power of 2");

	uint32_t halfLen = len>>1;
	float tempReal, tempImag;

	// Bit reversal sorting
	// This effectively does a multi-stage deinterlace, ex:
	// 1 8-point siganl:  0 1 2 3 4 5 6 7
	// 2 4-point signals: 0 2 4 6|1 3 5 7
	// 4 2-point signals: 0 4|2 6|1 5|3 7
	// 8 1-point signals: 0|4|2|6|1|5|3|7
	for (uint32_t i = 1, j = halfLen, k; i < len-1; i++, j+=k) {
		if (i < j) {
			tempReal = valReal[j];
			tempImag = valImag[j];
			valReal[j] = valReal[i];
			valImag[j] = valImag[i];
			valReal[i] = tempReal;
			valImag[i] = tempImag;
		}
		k = halfLen;
		while (k <= j) {
			j -= k;
			k >>= 1;
		}
	}

	// Loop for each stage
	// Start with the 2-point signals because 1-point signals would be unaltered anyway
	uint32_t levelLenOver2 = 1;
	for (uint32_t levelLen = 2; levelLen <= len; levelLenOver2 = levelLen, levelLen <<= 1) {
		float rotReal = 1.0f;
		float rotImag = 0.0f;
		// Calculate sine and cosine values of 1 point along our signal
		float cosReal =  cosf(AZA_TAU / (float)levelLen);
		float cosImag = -sinf(AZA_TAU / (float)levelLen);
		// Loop for each sub DFT
		for (uint32_t subDFT = 0; subDFT < levelLenOver2; subDFT++) {
			// Loop for each butterfly
			for (uint32_t i = subDFT; i < len; i += levelLen) {
				uint32_t ip = i + levelLenOver2;
				// Butterfly calculation
				tempReal    = valReal[ip]*rotReal - valImag[ip]*rotImag;
				tempImag    = valReal[ip]*rotImag + valImag[ip]*rotReal;
				valReal[ip] = valReal[i] - tempReal;
				valImag[ip] = valImag[i] - tempImag;
				valReal[i]  = valReal[i] + tempReal;
				valImag[i]  = valImag[i] + tempImag;
			}
			// Progress along the sinusoids
			tempReal = rotReal;
			rotReal = tempReal*cosReal - rotImag*cosImag;
			rotImag = tempReal*cosImag + rotImag*cosReal;
		}
	}
}