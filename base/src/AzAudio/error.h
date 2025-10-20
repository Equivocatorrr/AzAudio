/*
	File: error.h
	Author: Philip Haynes
	Defining error codes.
*/

#ifndef AZAUDIO_ERROR_H
#define AZAUDIO_ERROR_H

#include <stddef.h> // size_t

#ifdef __cplusplus
extern "C" {
#endif

enum {
	// The operation completed successfully
	AZA_SUCCESS=0,
	// A memory allocation failed
	AZA_ERROR_OUT_OF_MEMORY,
	// A backend is not available on this system
	AZA_ERROR_BACKEND_UNAVAILABLE,
	// Failed to initialize a backend
	AZA_ERROR_BACKEND_LOAD_ERROR,
	// A backend produced an error
	AZA_ERROR_BACKEND_ERROR,
	// There are no sound devices available to create a Stream
	AZA_ERROR_NO_DEVICES_AVAILABLE,
	// A pointer was unexpectedly null
	AZA_ERROR_NULL_POINTER,
	// A dsp function was given a buffer with no channels, or an otherwise incorrect number of channels for the specific DSP
	AZA_ERROR_INVALID_CHANNEL_COUNT,
	// A dsp function was given a buffer with no frames
	AZA_ERROR_INVALID_FRAME_COUNT,
	// Something wasn't configured right... check stderr
	AZA_ERROR_INVALID_CONFIGURATION,
	// Two buffers were expected to have the same number of channels, but they didn't
	AZA_ERROR_MISMATCHED_CHANNEL_COUNT,
	// Two buffers were expected to have the same number of frames, but they didn't
	AZA_ERROR_MISMATCHED_FRAME_COUNT,
	// Two buffers were expected to have the same samplerate, but they didn't
	AZA_ERROR_MISMATCHED_SAMPLERATE,
	// An azaDSP was expecting a single-buffer interface (like `int azaFilterProcess(azaFilter *data, azaBuffer buffer)`) and was given a dual-buffer interface
	AZA_ERROR_DSP_INTERFACE_EXPECTED_SINGLE,
	// An azaDSP was expecting a dual-buffer interface (like `int azaRMSProcessDual(azaRMS *data, azaBuffer dst, azaBuffer src)`) and was given a single-buffer interface
	AZA_ERROR_DSP_INTERFACE_EXPECTED_DUAL,
	// An azaDSP was used generically when its interface makes no sense as such (requires additional information)
	// TODO: Maybe make interfaces like this not a thing, allowing everything to be stored in the struct
	AZA_ERROR_DSP_INTERFACE_NOT_GENERIC,
	// Attempted to process an azaMixer with circular track routing
	AZA_ERROR_MIXER_ROUTING_CYCLE,
	// Enum count
	AZA_ERROR_ONE_AFTER_LAST,
};
// For known error codes, buffer is unused. For unknown error codes, prints into buffer and returns it.
const char *azaErrorString(int error, char *buffer, size_t bufferSize);

#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_ERROR_H