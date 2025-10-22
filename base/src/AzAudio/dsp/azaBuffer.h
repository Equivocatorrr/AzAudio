/*
	File: azaBuffer.h
	Author: Philip Haynes
	Audio buffer. You understand.
*/
#ifndef AZAUDIO_BUFFER_H
#define AZAUDIO_BUFFER_H

#include "../channel_layout.h"
#include "azaChannelMatrix.h"
#include "../easing.h"

#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

// TODO: azaBuffer might want to know when it's silence for optimization purposes
// TODO: azaBuffer could be expanded to allow leading and trailing frames, which would make it easy and almost free to integrate DSP latency directly without plugins needing their own buffers, and would be a silver bullet for latency compensation as well

// Buffer used by DSP functions for their input/output
typedef struct azaBuffer {
	float *pSamples; // Actual read/write-able data. Pointer is framesLeading*stride indices into a buffer.
	uint32_t samplerate; // Samples per second per channel, used by DSP functions that rely on timing.
	uint32_t frames; // How many samples there are in a single channel.
	uint32_t framesLeading; // We can have leading frames, used for sampling with kernels.
	uint32_t framesTrailing; // We can have trailing frames, used for sampling with kernels.
	uint16_t stride; // Distance between samples from one channel in number of floats.
	bool owned; // Only true if we're the owner of the buffer. Slices and user-managed memory buffers don't own the memory.
	uint8_t _reserved[5]; // Explicit padding bytes, reserved for later use.
	azaChannelLayout channelLayout; // channelLayout.count is always required. Some functions expect the layout to be fully-specified, others don't care.
} azaBuffer;

static_assert(sizeof(azaBuffer) == (sizeof(float*) + 24 + sizeof(azaChannelLayout)), "Please update the expected size of azaBuffer and remember to reserve padding explicitly.");

// context is a string that gets added to the error message to specify where the error is coming from
// May return AZA_ERROR_NULL_POINTER if buffer or buffer->pSamples is NULL
// May return AZA_ERROR_INVALID_CHANNEL_COUNT if channelLayout.count < 1 or > AZA_MAX_CHANNEL_POSITIONS
// May return AZA_ERROR_INVALID_FRAME_COUNT if total frames < 1 or if total frames * channelLayout.count would overflow a uint32_t (total frames is frames + framesLeading + framesTrailing)
int azaCheckBuffer_full(const char *context, azaBuffer *buffer);

#define azaCheckBuffer(buffer) azaCheckBuffer_full(AZA_FUNCTION_NAME, buffer)

// context is a string that gets added to the error message to specify where the error is coming from
// calls azaCheckBuffer on both buffers, inheriting all possible errors
// if sameFrameCount is true, then it may return AZA_ERROR_MISMATCHED_FRAME_COUNT
// if sameChannelCount is true, then it may return AZA_ERROR_MISMATCHED_CHANNEL_COUNT
int azaCheckBuffersForDSPProcess_full(const char *context, azaBuffer *dst, azaBuffer *src, bool sameFrameCount, bool sameChannelCount);

#define azaCheckBuffersForDSPProcess(dst, src, sameFrameCount, sameChannelCount) azaCheckBuffersForDSPProcess_full(AZA_FUNCTION_NAME, dst, src, sameFrameCount, sameChannelCount)

// returns the pointer to the beginning of the allocation
// asserts that pSamples is not NULL
static inline float* azaBufferGetStart(azaBuffer *buffer) {
	assert(buffer->pSamples != NULL);
	return buffer->pSamples - (buffer->framesLeading * buffer->stride);
}

// returns the frame count including leading and trailing frames
static inline uint32_t azaBufferGetTotalFrameCount(azaBuffer *buffer) {
	return buffer->frames + buffer->framesLeading + buffer->framesTrailing;
}

// returns an unowned azaBuffer whose internal range represents the whole of buffer and its leading and trailing frames.
static inline azaBuffer azaBufferGetExtended(azaBuffer *buffer) {
	azaBuffer result = *buffer;
	result.pSamples = azaBufferGetStart(buffer);
	result.frames = azaBufferGetTotalFrameCount(buffer);
	result.framesLeading = 0;
	result.framesTrailing = 0;
	result.owned = false;
	return result;
}

static inline float azaBufferGetLen_ms(azaBuffer *buffer) {
	return 1000.0f * (float)buffer->frames / (float)buffer->samplerate;
}

// If samples are externally-managed, you don't have to call azaBufferInit or azaBufferDeinit
// NOTE: asserts frames > 0 and channelLayout.count > 0
// May return AZA_ERROR_OUT_OF_MEMORY
int azaBufferInit(azaBuffer *buffer, uint32_t frames, uint32_t framesLeading, uint32_t framesTrailing, azaChannelLayout channelLayout);
// if warnOnUnowned is true, then if data points to an unowned buffer, we'll log a warning. Otherwise we just do the smart thing silently.
void azaBufferDeinit(azaBuffer *buffer, bool warnOnUnowned);

// Zeroes out an entire buffer, including leading and trailing frames.
void azaBufferZero(azaBuffer *buffer);

// buffers are defined to be interlaced (where channels come one after the other in memory for a single frame)
// But some operations are MUCH faster on deinterlaced data, so this will copy the buffer to a secondary buffer to shuffle the data around such that all the samples of a single channel are adjacent to each other.

// This operation is MUCH simpler with a sidebuffer to copy to, and a Deinterlace will pretty much always be paired with an Interlace, so if you hold on to the sidebuffer and use that for processing, you'll effectively eliminate 2 full buffer copies. This also means that if there's only 1 channel, you might consider handling that specially and eliminating the sidebuffer (because otherwise we're just doing an unnecessary buffer copy).
// NOTE: asserts that dst and src have the same frame count, and that the stride, channel counts all match (stride must be the same as channel count)
void azaBufferDeinterlace(azaBuffer *dst, azaBuffer *src);
void azaBufferReinterlace(azaBuffer *dst, azaBuffer *src);

// Mixes src into the existing contents of dst
// NOTE: This will not respect channel positions. The buffers will be mixed as though the channel layouts are the same.
// NOTE: asserts that dst and src have the same frame count and channel count.
// For arbitrary channel mixing, use azaBufferMixMatrix.
void azaBufferMix(azaBuffer *dst, float volumeDst, azaBuffer *src, float volumeSrc);

// Same as azaBufferMix, but the volumes will fade across the buffer according to the easing functions.
// You can pass NULL into each easing function and it will default to linear.
// For uncorrelated signals, cosine crossfades maintain unity power (but may peak up to sqrt(2)).
// For correlated signals, linear crossfades maintain unity power (and cannot peak higher than 1).
// A cosine crossfade from dst to src would use easeDst=azaEaseCosineOut and easeSrc=azaEaseCosineIn.
// NOTE: asserts that dst and src have the same frame count and channel count.
void azaBufferMixFade(azaBuffer *dst, float volumeDstStart, float volumeDstEnd, fp_azaEase_t easeDst, azaBuffer *src, float volumeSrcStart, float volumeSrcEnd, fp_azaEase_t easeSrc);

// Copies the contents of one buffer into the other.
// NOTE: asserts that dst and src have the same frame count and channel count.
void azaBufferCopy(azaBuffer *dst, azaBuffer *src);

// Copies the contents of one channel of src into dst
// NOTE: asserts that dst and src have the same frame count and that the channels are in range.
void azaBufferCopyChannel(azaBuffer *dst, uint8_t channelDst, azaBuffer *src, uint8_t channelSrc);

// Copies one channel from src into all channels of dst
// NOTE: asserts that dst and src have the same frame count and that channelSrc is in range.
void azaBufferBroadcastChannel(azaBuffer *dst, azaBuffer *src, uint8_t channelSrc);

// Get an unowned view into an existing buffer, offset by frameStart, with a length of frameCount
// Automatically expands framesLeading and framesTrailing to include the entirety of src
// NOTE: asserts that the frameStart and frameCount represent a valid range within src
azaBuffer azaBufferSlice(azaBuffer *src, uint32_t frameStart, uint32_t frameCount);

// Get an unowned view into an existing buffer that points to a single channel
azaBuffer azaBufferOneChannel(azaBuffer *src, uint8_t channel);

// Get an unowned view at one singular float, represented as an azaBuffer
azaBuffer azaBufferOneSample(float *sample, uint32_t samplerate);

// Similar to azaBufferMix, except you pass in a matrix for channel mixing.
// NOTE: asserts that dst and src have the same frame count
// NOTE: asserts that the matrix has the right number of inputs and outputs for the buffers
void azaBufferMixMatrix(azaBuffer *dst, float volumeDst, azaBuffer *src, float volumeSrc, azaChannelMatrix *matrix);



// Side Buffers, because sometimes you need extra buffers for processing.
// We maintain a small stack of side buffers.
// TODO: Make them allocate from a single arena rather than having individual buffers.



azaBuffer azaPushSideBuffer(uint32_t frames, uint32_t framesLeading, uint32_t framesTrailing, uint32_t channels, uint32_t samplerate);

azaBuffer azaPushSideBufferZero(uint32_t frames, uint32_t framesLeading, uint32_t framesTrailing, uint32_t channels, uint32_t samplerate);

azaBuffer azaPushSideBufferCopy(azaBuffer *src);

azaBuffer azaPushSideBufferCopyZero(azaBuffer *ref);

void azaPopSideBuffer();

void azaPopSideBuffers(uint8_t count);



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_BUFFER_H