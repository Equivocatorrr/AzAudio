/*
	File: dps.h
	Author: Philip Haynes
	structs and functions for digital signal processing
*/

#ifndef AZAUDIO_DSP_H
#define AZAUDIO_DSP_H

#include <stdlib.h>

#include "header_utils.h"
#include "math.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AZAUDIO_LOOKAHEAD_SAMPLES 128
// The duration of transitions between the variable parameter values
#define AZAUDIO_SAMPLER_TRANSITION_FRAMES 128



// TODO: The most extreme setups can have 20 channels, but there may be no way to actually use that many channels effectively without a way to distinguish them (and the below are all you can get from the MMDevice API on Windows afaik). Should there be more information available, this part of the API will grow.
// TODO: Find a way to get more precise speaker placement information, if that's even possible.
/* These roughly correspond to the following physical positions.
Floor:
    6 2 7
  0       1
 9    H    10
  4   8   5

Ceiling:
  12 13 14
     H
  15 16 17
*/
enum azaPosition {
	AZA_POS_LEFT_FRONT         = 0,
	AZA_POS_RIGHT_FRONT        = 1,
	AZA_POS_CENTER_FRONT       = 2,
	AZA_POS_SUBWOOFER          = 3,
	AZA_POS_LEFT_BACK          = 4,
	AZA_POS_RIGHT_BACK         = 5,
	AZA_POS_LEFT_CENTER_FRONT  = 6,
	AZA_POS_RIGHT_CENTER_FRONT = 7,
	AZA_POS_CENTER_BACK        = 8,
	AZA_POS_LEFT_SIDE          = 9,
	AZA_POS_RIGHT_SIDE         =10,
	AZA_POS_CENTER_TOP         =11,
	AZA_POS_LEFT_FRONT_TOP     =12,
	AZA_POS_CENTER_FRONT_TOP   =13,
	AZA_POS_RIGHT_FRONT_TOP    =14,
	AZA_POS_LEFT_BACK_TOP      =15,
	AZA_POS_CENTER_BACK_TOP    =16,
	AZA_POS_RIGHT_BACK_TOP     =17,
};
// NOTE: This is more than we should ever see in reality, and definitely more than can be uniquely represented by the above positions. We're reserving more for later.
#define AZA_MAX_CHANNEL_POSITIONS 22
#define AZA_POS_ENUM_COUNT (AZA_POS_RIGHT_BACK_TOP+1)

enum azaFormFactor {
	AZA_FORM_FACTOR_SPEAKERS=0,
	AZA_FORM_FACTOR_HEADPHONES,
};

typedef struct azaChannelLayout {
	uint8_t count;
	uint8_t formFactor;
	uint8_t positions[AZA_MAX_CHANNEL_POSITIONS];
} azaChannelLayout;

// Some standard layouts, for your convenience

static inline azaChannelLayout azaChannelLayoutMono() {
	return AZA_CLITERAL(azaChannelLayout) {
		/* .count      = */ 1,
		/* .formFactor = */ AZA_FORM_FACTOR_SPEAKERS,
		/* .positions  = */ {AZA_POS_CENTER_FRONT},
	};
}

static inline azaChannelLayout azaChannelLayoutStereo() {
	return AZA_CLITERAL(azaChannelLayout) {
		/* .count      = */ 2,
		/* .formFactor = */ AZA_FORM_FACTOR_SPEAKERS,
		/* .positions  = */ { AZA_POS_LEFT_FRONT, AZA_POS_RIGHT_FRONT },
	};
}

static inline azaChannelLayout azaChannelLayoutHeadphones() {
	return AZA_CLITERAL(azaChannelLayout) {
		/* .count      = */ 2,
		/* .formFactor = */ AZA_FORM_FACTOR_HEADPHONES,
		/* .positions  = */ { AZA_POS_LEFT_FRONT, AZA_POS_RIGHT_FRONT },
	};
}

static inline azaChannelLayout azaChannelLayout_2_1() {
	return AZA_CLITERAL(azaChannelLayout) {
		/* .count      = */ 3,
		/* .formFactor = */ AZA_FORM_FACTOR_SPEAKERS,
		/* .positions  = */ { AZA_POS_LEFT_FRONT, AZA_POS_RIGHT_FRONT, AZA_POS_SUBWOOFER },
	};
}

static inline azaChannelLayout azaChannelLayout_3_0() {
	return AZA_CLITERAL(azaChannelLayout) {
		/* .count      = */ 3,
		/* .formFactor = */ AZA_FORM_FACTOR_SPEAKERS,
		/* .positions  = */ { AZA_POS_LEFT_FRONT, AZA_POS_RIGHT_FRONT, AZA_POS_CENTER_FRONT },
	};
}

static inline azaChannelLayout azaChannelLayout_3_1() {
	return AZA_CLITERAL(azaChannelLayout) {
		/* .count      = */ 4,
		/* .formFactor = */ AZA_FORM_FACTOR_SPEAKERS,
		/* .positions  = */ { AZA_POS_LEFT_FRONT, AZA_POS_RIGHT_FRONT, AZA_POS_CENTER_FRONT, AZA_POS_SUBWOOFER },
	};
}

static inline azaChannelLayout azaChannelLayout_4_0() {
	return AZA_CLITERAL(azaChannelLayout) {
		/* .count      = */ 4,
		/* .formFactor = */ AZA_FORM_FACTOR_SPEAKERS,
		/* .positions  = */ { AZA_POS_LEFT_FRONT, AZA_POS_RIGHT_FRONT, AZA_POS_LEFT_BACK, AZA_POS_RIGHT_BACK },
	};
}

static inline azaChannelLayout azaChannelLayout_4_1() {
	return AZA_CLITERAL(azaChannelLayout) {
		/* .count      = */ 5,
		/* .formFactor = */ AZA_FORM_FACTOR_SPEAKERS,
		/* .positions  = */ { AZA_POS_LEFT_FRONT, AZA_POS_RIGHT_FRONT, AZA_POS_SUBWOOFER, AZA_POS_LEFT_BACK, AZA_POS_RIGHT_BACK },
	};
}

static inline azaChannelLayout azaChannelLayout_5_0() {
	return AZA_CLITERAL(azaChannelLayout) {
		/* .count      = */ 5,
		/* .formFactor = */ AZA_FORM_FACTOR_SPEAKERS,
		/* .positions  = */ { AZA_POS_LEFT_FRONT, AZA_POS_RIGHT_FRONT, AZA_POS_CENTER_FRONT, AZA_POS_LEFT_BACK, AZA_POS_RIGHT_BACK },
	};
}

static inline azaChannelLayout azaChannelLayout_5_1() {
	return AZA_CLITERAL(azaChannelLayout) {
		/* .count      = */ 6,
		/* .formFactor = */ AZA_FORM_FACTOR_SPEAKERS,
		/* .positions  = */ { AZA_POS_LEFT_FRONT, AZA_POS_RIGHT_FRONT, AZA_POS_CENTER_FRONT, AZA_POS_SUBWOOFER, AZA_POS_LEFT_BACK, AZA_POS_RIGHT_BACK },
	};
}

static inline azaChannelLayout azaChannelLayout_7_0() {
	return AZA_CLITERAL(azaChannelLayout) {
		/* .count      = */ 7,
		/* .formFactor = */ AZA_FORM_FACTOR_SPEAKERS,
		/* .positions  = */ { AZA_POS_LEFT_FRONT, AZA_POS_RIGHT_FRONT, AZA_POS_CENTER_FRONT, AZA_POS_LEFT_BACK, AZA_POS_RIGHT_BACK, AZA_POS_LEFT_SIDE, AZA_POS_RIGHT_SIDE },
	};
}

static inline azaChannelLayout azaChannelLayout_7_1() {
	return AZA_CLITERAL(azaChannelLayout) {
		/* .count      = */ 8,
		/* .formFactor = */ AZA_FORM_FACTOR_SPEAKERS,
		/* .positions  = */ { AZA_POS_LEFT_FRONT, AZA_POS_RIGHT_FRONT, AZA_POS_CENTER_FRONT, AZA_POS_SUBWOOFER, AZA_POS_LEFT_BACK, AZA_POS_RIGHT_BACK, AZA_POS_LEFT_SIDE, AZA_POS_RIGHT_SIDE },
	};
}

static inline azaChannelLayout azaChannelLayout_9_0() {
	return AZA_CLITERAL(azaChannelLayout) {
		/* .count      = */ 9,
		/* .formFactor = */ AZA_FORM_FACTOR_SPEAKERS,
		/* .positions  = */ { AZA_POS_LEFT_FRONT, AZA_POS_RIGHT_FRONT, AZA_POS_CENTER_FRONT, AZA_POS_LEFT_BACK, AZA_POS_RIGHT_BACK, AZA_POS_LEFT_CENTER_FRONT, AZA_POS_RIGHT_CENTER_FRONT, AZA_POS_LEFT_SIDE, AZA_POS_RIGHT_SIDE },
	};
}

static inline azaChannelLayout azaChannelLayout_9_1() {
	return AZA_CLITERAL(azaChannelLayout) {
		/* .count      = */ 10,
		/* .formFactor = */ AZA_FORM_FACTOR_SPEAKERS,
		/* .positions  = */ { AZA_POS_LEFT_FRONT, AZA_POS_RIGHT_FRONT, AZA_POS_CENTER_FRONT, AZA_POS_SUBWOOFER, AZA_POS_LEFT_BACK, AZA_POS_RIGHT_BACK, AZA_POS_LEFT_CENTER_FRONT, AZA_POS_RIGHT_CENTER_FRONT, AZA_POS_LEFT_SIDE, AZA_POS_RIGHT_SIDE },
	};
}

// Make a reasonable guess about the layout for 1 to 10 channels. For more advanced layouts, such as with aerial speakers, you'll have to specify them manually, since there's no way to guess the setup in a meaningful way for aerials.
// To use the device layout on a Stream, just leave channels zeroed out.
static inline azaChannelLayout azaChannelLayoutStandardFromCount(uint8_t count) {
	switch (count) {
		case  1: return azaChannelLayoutMono();
		case  2: return azaChannelLayoutStereo();
		case  3: return azaChannelLayout_2_1();
		case  4: return azaChannelLayout_4_0();
		case  5: return azaChannelLayout_5_0();
		case  6: return azaChannelLayout_5_1();
		case  7: return azaChannelLayout_7_0();
		case  8: return azaChannelLayout_7_1();
		case  9: return azaChannelLayout_9_0();
		case 10: return azaChannelLayout_9_1();
		default: return AZA_CLITERAL(azaChannelLayout) { 0 };
	}
}



// Buffer used by DSP functions for their input/output
typedef struct azaBuffer {
	// actual read/write-able data
	// one frame is a single sample from each channel, one after the other
	float *samples;
	// samples per second, used by DSP functions that rely on timing
	uint32_t samplerate;
	// how many samples there are in a single channel
	uint32_t frames;
	// distance between samples from one channel in number of floats
	uint16_t stride;
	// how many channels are stored in this buffer for user-created buffers, or how many channels should be accessed by DSP functions, and an optional layout for said channels. Some functions expect the layout to be fully-specified, others don't care.
	azaChannelLayout channels;
} azaBuffer;
// You must first set frames and channels before calling this to allocate samples.
// If samples are externally-managed, you don't have to do this.
int azaBufferInit(azaBuffer *data);
int azaBufferDeinit(azaBuffer *data);

// Mixes src into the existing contents of dst
// NOTE: This will not respect channel positions. The buffers will be mixed as though the channel layouts are the same.
void azaBufferMix(azaBuffer dst, float volumeDst, azaBuffer src, float volumeSrc);

// Same as azaBufferMix, but the volumes will fade linearly across the buffer
void azaBufferMixFade(azaBuffer dst, float volumeDstStart, float volumeDstEnd, azaBuffer src, float volumeSrcStart, float volumeSrcEnd);

// Copies the contents of one buffer into the other. They must have the same number of frames and channels.
void azaBufferCopy(azaBuffer dst, azaBuffer src);

// Copies the contents of one channel of src into dst
void azaBufferCopyChannel(azaBuffer dst, uint8_t channelDst, azaBuffer src, uint8_t channelSrc);

static inline azaBuffer azaBufferOneSample(float *sample, uint32_t samplerate) {
	return AZA_CLITERAL(azaBuffer) {
		/* .samples    = */ sample,
		/* .samplerate = */ samplerate,
		/* .frames     = */ 1,
		/* .stride     = */ 1,
		/* .channels   = */ azaChannelLayoutMono(),
	};
}



typedef enum azaDSPKind {
	AZA_DSP_NONE=0,
	AZA_DSP_RMS,
	AZA_DSP_LOOKAHEAD_LIMITER,
	AZA_DSP_FILTER,
	AZA_DSP_COMPRESSOR,
	AZA_DSP_DELAY,
	AZA_DSP_REVERB,
	AZA_DSP_SAMPLER,
	AZA_DSP_GATE,
} azaDSPKind;

// Generic interface to all the DSP structures
typedef struct azaDSP {
	azaDSPKind kind;
	uint32_t structSize;
	struct azaDSP *pNext;
} azaDSP;
int azaProcessDSP(azaBuffer buffer, azaDSP *data);



// Must be at the end of your DSP struct as it can have inline channel data
typedef struct azaDSPChannelData {
	uint8_t capInline;
	uint8_t capAdditional;
	uint8_t countActive;
	uint8_t alignment;
	uint32_t size;
	void *additional;
	// azaXXXChannelData inline[capInline];
} azaDSPChannelData;



typedef void (*fp_azaOp)(float *lhs, float rhs);
void azaOpAdd(float *lhs, float rhs);
void azaOpMax(float *lhs, float rhs);



typedef struct azaRMSConfig {
	uint32_t windowSamples;
} azaRMSConfig;

typedef struct azaRMSChannelData {
	float squaredSum;
} azaRMSChannelData;

typedef struct azaRMS {
	azaDSP header;
	azaRMSConfig config;
	uint32_t index;
	uint32_t bufferCap;
	float *buffer;
	azaDSPChannelData channelData;
	// float inlineBuffer[256]; // optional
} azaRMS;
azaRMS* azaMakeRMS(azaRMSConfig config, uint8_t channelCapInline);
void azaFreeRMS(azaRMS *data);
// Takes the rms of all the channels combined with op and puts that into the first channel of dst
int azaProcessRMSCombined(azaBuffer dst, azaBuffer src, azaRMS *data, fp_azaOp op);
int azaProcessRMS(azaBuffer buffer, azaRMS *data);



int azaProcessCubicLimiter(azaBuffer buffer);



typedef struct azaLookaheadLimiterConfig {
	// input gain in dB
	float gainInput;
	// output gain in dB
	float gainOutput;
} azaLookaheadLimiterConfig;

typedef struct azaLookaheadLimiterChannelData {
	float valBuffer[AZAUDIO_LOOKAHEAD_SAMPLES];
} azaLookaheadLimiterChannelData;

// NOTE: This limiter increases latency by AZAUDIO_LOOKAHEAD_SAMPLES samples
typedef struct azaLookaheadLimiter {
	azaDSP header;
	azaLookaheadLimiterConfig config;

	// Data shared by all channels
	float gainBuffer[AZAUDIO_LOOKAHEAD_SAMPLES];
	int index;
	float sum;

	azaDSPChannelData channelData;
} azaLookaheadLimiter;
azaLookaheadLimiter* azaMakeLookaheadLimiter(azaLookaheadLimiterConfig config, uint8_t channelCapInline);
void azaFreeLookaheadLimiter(azaLookaheadLimiter *data);
int azaProcessLookaheadLimiter(azaBuffer buffer, azaLookaheadLimiter *data);



typedef enum azaFilterKind {
	AZA_FILTER_HIGH_PASS,
	AZA_FILTER_LOW_PASS,
	AZA_FILTER_BAND_PASS,
} azaFilterKind;

typedef struct azaFilterConfig {
	azaFilterKind kind;
	// Cutoff frequency in Hz
	float frequency;
	// Blends the effect output with the dry signal where 1 is fully dry and 0 is fully wet.
	float dryMix;
} azaFilterConfig;

typedef struct azaFilterChannelData {
	float outputs[2];
} azaFilterChannelData;

typedef struct azaFilter {
	azaDSP header;
	azaFilterConfig config;
	azaDSPChannelData channelData;
} azaFilter;
azaFilter* azaMakeFilter(azaFilterConfig config, uint8_t channelCapInline);
void azaFreeFilter(azaFilter *data);
int azaProcessFilter(azaBuffer buffer, azaFilter *data);



typedef struct azaCompressorConfig {
	// Activation threshold in dB
	float threshold;
	// positive values allow 1/ratio of the overvolume through
	// negative values subtract overvolume*ratio
	float ratio;
	// attack time in ms
	float attack;
	// decay time in ms
	float decay;
} azaCompressorConfig;

typedef struct azaCompressor {
	azaDSP header;
	azaCompressorConfig config;
	azaRMS *rms;
	float attenuation;
	float gain; // For monitoring/debugging
} azaCompressor;
azaCompressor* azaMakeCompressor(azaCompressorConfig config);
void azaFreeCompressor(azaCompressor *data);
int azaProcessCompressor(azaBuffer buffer, azaCompressor *data);



typedef struct azaDelayConfig {
	// effect gain in dB
	float gain;
	// dry gain in dB
	float gainDry;
	// delay time in ms
	float delay;
	// 0 to 1 multiple of output feeding back into input
	float feedback;
	// You can provide a chain of effects to operate on the wet output
	azaDSP *wetEffects;
} azaDelayConfig;

typedef struct azaDelayChannelData {
	float *buffer;
	uint32_t delaySamples;
	uint32_t index;
	// extra delay time in ms
	float delay;
} azaDelayChannelData;

typedef struct azaDelay {
	azaDSP header;
	azaDelayConfig config;
	// Combined big buffer that gets split for each channel
	float *buffer;
	uint32_t bufferCap;
	azaDSPChannelData channelData;
} azaDelay;
azaDelay* azaMakeDelay(azaDelayConfig config, uint8_t channelCapInline);
void azaFreeDelay(azaDelay *data);
int azaProcessDelay(azaBuffer buffer, azaDelay *data);
// Call this for each channel individually
azaDelayChannelData* azaDelayGetChannelData(azaDelay *data, uint8_t channel);



typedef struct azaReverbConfig {
	// effect gain in dB
	float gain;
	// dry gain in dB
	float gainDry;
	// value affecting reverb feedback, roughly in the range of 1 to 100 for reasonable results
	float roomsize;
	// value affecting damping of high frequencies, roughly in the range of 1 to 5
	float color;
	// delay for first reflections in ms
	float delay;
} azaReverbConfig;

#define AZAUDIO_REVERB_DELAY_COUNT 15
typedef struct azaReverb {
	azaDSP header;
	azaReverbConfig config;
	azaDelay *delays[AZAUDIO_REVERB_DELAY_COUNT];
	azaFilter *filters[AZAUDIO_REVERB_DELAY_COUNT];
} azaReverb;
azaReverb* azaMakeReverb(azaReverbConfig config, uint8_t channelCapInline);
void azaFreeReverb(azaReverb *data);
int azaProcessReverb(azaBuffer buffer, azaReverb *data);



typedef struct azaSamplerConfig {
	// buffer containing the sound we're sampling
	azaBuffer *buffer;
	// playback speed as a multiple where 1 is full speed
	float speed;
	// volume of effect in dB
	float gain;
} azaSamplerConfig;

typedef struct azaSampler {
	azaDSP header;
	azaSamplerConfig config;
	float frame;
	float s; // Smooth speed
	float g; // Smooth gain
} azaSampler;
azaSampler* azaMakeSampler(azaSamplerConfig config);
void azaFreeSampler(azaSampler *data);
int azaProcessSampler(azaBuffer buffer, azaSampler *data);



typedef struct azaGateConfig {
	// cutoff threshold in dB
	float threshold;
	// attack time in ms
	float attack;
	// decay time in ms
	float decay;
	// Any effects to apply to the activation signal
	azaDSP *activationEffects;
} azaGateConfig;

typedef struct azaGate {
	azaDSP header;
	azaGateConfig config;
	azaRMS *rms;
	float attenuation;
	float gain;
} azaGate;
azaGate* azaMakeGate(azaGateConfig config);
void azaFreeGate(azaGate *data);
int azaProcessGate(azaBuffer buffer, azaGate *data);



typedef struct azaKernel {
	// if this is 1, we only store half of the actual table
	int isSymmetrical;
	// length of the kernel, which is half of the actual length if we're symmetrical
	float length;
	// How many samples there are between an interval of length 1
	float scale;
	// total size of table, which is length * scale
	uint32_t size;
	float *table;
} azaKernel;

// Creates a blank kernel
void azaKernelInit(azaKernel *kernel, int isSymmetrical, float length, float scale);
void azaKernelDeinit(azaKernel *kernel);

float azaKernelSample(azaKernel *kernel, float x);

// Makes a lanczos kernel. resolution is the number of samples between zero crossings
void azaKernelMakeLanczos(azaKernel *kernel, float resolution, float radius);


float azaSampleWithKernel(float *src, int stride, int minFrame, int maxFrame, azaKernel *kernel, float pos);

// Performs resampling of src into dst with the given scaling factor and kernel.
// srcFrames is not actually needed here because the sampleable extent is provided by srcFrameMin and srcFrameMax, but for this description it refers to how many samples within src are considered the "meat" of the signal (excluding padding carried over from the last iteration of resampling a stream).
// factor is the scaling ratio (defined roughly as `srcFrames / dstFrames`), passed in explicitly because the exact desired ratio may not be represented accurately by a ratio of the length of two small buffers. For no actual time scaling, this ratio should be perfectly represented by `srcSamplerate / dstSamplerate`.
// src should point at least `-srcFrameMin` frames into an existing source buffer with a total extent of `srcFrameMax-srcFrameMin`.
// srcFrameMin and srcFrameMax allow the accessible extent of src to go outside of the given 0...srcFrames extent, since that's required for perfect resampling of chunks of a stream (while accepting some latency). Ideally, srcFrameMin would be `-kernel->size` and srcFrameMax would be `srcFrames+kernel->size` for a symmetric kernel. For a non-symmetric kernel, srcFrameMin can be 0, and srcFrameMax would still be srcFrames+kernel->size. For two isolated buffers, srcFrameMin should be 0 and srcFrameMax should be srcFrames. Any samples outside of this extent will be considered to be zeroes.
// srcSampleOffset should be in the range 0 to 1
void azaResample(azaKernel *kernel, float factor, float *dst, int dstStride, int dstFrames, float *src, int srcStride, int srcFrameMin, int srcFrameMax, float srcSampleOffset);

// Same as azaResample, except the resampled values are added to dst instead of replacing them. Every sample is multiplied by amp before being added.
void azaResampleAdd(azaKernel *kernel, float factor, float amp, float *dst, int dstStride, int dstFrames, float *src, int srcStride, int srcFrameMin, int srcFrameMax, float srcSampleOffset);


typedef struct azaWorld {
	// Position of our ears
	azaVec3 origin;
	// Must be an orthogonal matrix
	azaMat3 orientation;
	// Speed of sound in units per second.
	// Default: 343.0f (speed of sound in dry air at 20C in m/s)
	float speedOfSound;
} azaWorld;
extern azaWorld azaWorldDefault;

// Does simple volume-based spatialization of the source to map it to the channel layout.
// Adds its sound to the existing signal in dstBuffer.
// dstBuffer and srcBuffer must have the same number of frames and the same samplerate.
// srcBuffer MUST be 1-channel
// world can be NULL, indicating to use azaWorldDefault.
// Doesn't attenuate the volume by distance. You must do that yourself and pass the result into srcAmp.
void azaSpatializeSimple(azaBuffer dstBuffer, azaBuffer srcBuffer, azaVec3 srcPosStart, float srcAmpStart, azaVec3 srcPosEnd, float srcAmpEnd, const azaWorld *world);



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_DSP_H