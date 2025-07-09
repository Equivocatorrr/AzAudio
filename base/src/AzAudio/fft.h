/*
	File: fft.h
	Author: Philip Haynes
	Fast Fourier Transform
*/

#ifndef AZAUDIO_FFT_H
#define AZAUDIO_FFT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// len should be a power of 2
// For time-domain signals valReal should contain len samples and valImag should be len zeroes.
// The result will put len/2+1 values into valReal and valImag
// The output valReal[i] and valImag[i] correspond to i*samplerate/len Hz
// TODO: Study and document more properties of this function, including how to reverse it and what the remaining len/2-1 values in the output mean.
void azaFFT(float * restrict valReal, float * restrict valImag, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_FFT_H