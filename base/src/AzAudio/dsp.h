/*
	File: dps.h
	Author: Philip Haynes
	structs and functions for digital signal processing
*/

#ifndef AZAUDIO_DSP_H
#define AZAUDIO_DSP_H

#include "header_utils.h"
#include "math.h"
#include "channel_layout.h"
#include "utility.h"
#include "backend/threads.h"

#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

// TODO: Work on making the lookahead limiter more transparent without necessarily increasing latency. Perhaps find a way to make our linear envelope more like an S-curve? Also probably make the lookahead time configurable.
// 128 samples at 48.0kHz is 128.0/48.0=2.7ms
// 128 samples at 44.1kHz is 128.0/44.1=2.9ms
//  64 samples at 48.0kHz is  64.0/48.0=1.3ms
//  64 samples at 44.1kHz is  64.0/44.1=1.5ms
#define AZAUDIO_LOOKAHEAD_SAMPLES 128
// The duration of transitions between the variable parameter values
#define AZAUDIO_SAMPLER_TRANSITION_FRAMES 128

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
	azaChannelLayout channelLayout;
} azaBuffer;
// If samples are externally-managed, you don't have to call azaBufferInit or azaBufferDeinit
// May return AZA_ERROR_INVALID_FRAME_COUNT if frames == 0
// May return AZA_ERROR_INVALID_CHANNEL_COUNT if channelLayout.count == 0
// May return AZA_ERROR_OUT_OF_MEMORY if the allocation of data->samples failed
int azaBufferInit(azaBuffer *data, uint32_t frames, azaChannelLayout channelLayout);
void azaBufferDeinit(azaBuffer *data);

// Zeroes out an entire buffer
void azaBufferZero(azaBuffer buffer);

// buffers are defined to be interlaced (where channels come one after the other in memory for a single frame)
// But some operations are MUCH faster on deinterlaced data, so this will copy the buffer to a secondary buffer to shuffle the data around such that all the samples of a single channel are adjacent to each other.

// This operation is MUCH simpler with a sidebuffer to copy to, and a Deinterlace will pretty much always be paired with an Interlace, so if you hold on to the sidebuffer and use that for processing, you'll effectively eliminate 2 full buffer copies. This also means that if there's only 1 channel, you might consider handling that specially and eliminating the sidebuffer (because otherwise we're just doing an unnecessary buffer copy).
// NOTE: asserts that dst and src have the same frame count, and that the stride, channel counts all match (stride must be the same as channel count)
void azaBufferDeinterlace(azaBuffer dst, azaBuffer src);
void azaBufferReinterlace(azaBuffer dst, azaBuffer src);

// Mixes src into the existing contents of dst
// NOTE: This will not respect channel positions. The buffers will be mixed as though the channel layouts are the same.
// NOTE: asserts that dst and src have the same frame count and channel count. If that's a problem, then handle these conditions before calling this. Alternatively, use azaBufferMixMatrix.
void azaBufferMix(azaBuffer dst, float volumeDst, azaBuffer src, float volumeSrc);

// Same as azaBufferMix, but the volumes will fade linearly across the buffer
// NOTE: asserts that dst and src have the same frame count and channel count. If that's a problem, then handle these conditions before calling this.
void azaBufferMixFade(azaBuffer dst, float volumeDstStart, float volumeDstEnd, azaBuffer src, float volumeSrcStart, float volumeSrcEnd);

// Copies the contents of one buffer into the other. They must have the same number of frames and channels.
// NOTE: asserts that dst and src have the same frame count and channel count. If that's a problem, then handle these conditions before calling this.
void azaBufferCopy(azaBuffer dst, azaBuffer src);

// Copies the contents of one channel of src into dst
// NOTE: asserts that dst and src have the same frame count and that the channels are in range. If that's a problem, then handle these conditions before calling this.
void azaBufferCopyChannel(azaBuffer dst, uint8_t channelDst, azaBuffer src, uint8_t channelSrc);

static inline azaBuffer azaBufferSlice(azaBuffer src, uint32_t frameStart, uint32_t frameCount) {
	assert(frameStart < src.frames);
	assert(frameCount <= src.frames - frameStart);
	return AZA_CLITERAL(azaBuffer) {
		/* .samples       = */ src.samples + frameStart * src.stride,
		/* .samplerate    = */ src.samplerate,
		/* .frames        = */ frameCount,
		/* .stride        = */ src.stride,
		/* .channelLayout = */ src.channelLayout,
	};
}

static inline azaBuffer azaBufferOneSample(float *sample, uint32_t samplerate) {
	return AZA_CLITERAL(azaBuffer) {
		/* .samples       = */ sample,
		/* .samplerate    = */ samplerate,
		/* .frames        = */ 1,
		/* .stride        = */ 1,
		/* .channelLayout = */ azaChannelLayoutMono(),
	};
}

static inline azaBuffer azaBufferOneChannel(azaBuffer src, uint8_t channel) {
	return AZA_CLITERAL(azaBuffer) {
		/* .samples       = */ src.samples + channel,
		/* .samplerate    = */ src.samplerate,
		/* .frames        = */ src.frames,
		/* .stride        = */ src.stride,
		/* .channelLayout = */ azaChannelLayoutOneChannel(src.channelLayout, channel),
	};
}

typedef struct azaChannelMatrix {
	uint8_t inputs, outputs;
	float *matrix;
} azaChannelMatrix;

// allocates the matrix initialized to all zeroes
// May return AZA_ERROR_OUT_OF_MEMORY
int azaChannelMatrixInit(azaChannelMatrix *data, uint8_t inputs, uint8_t outputs);
void azaChannelMatrixDeinit(azaChannelMatrix *data);

// Expects data to have been initted with srcLayout.count cols and dstLayout.count rows
// Also assumes the existing values in the matrix are all zero (won't set to zero if they're not)
void azaChannelMatrixGenerateRoutingFromLayouts(azaChannelMatrix *data, azaChannelLayout srcLayout, azaChannelLayout dstLayout);


// Similar to azaBufferMix, except you pass in a matrix for channel mixing.
// Matrix columns represent the mapping to dst channels, while the rows represent the mapping from src channels.
// NOTE: asserts that dst and src have the same frame count
void azaBufferMixMatrix(azaBuffer dst, float volumeDst, azaBuffer src, float volumeSrc, azaChannelMatrix *matrix);

typedef int (*fp_azaProcessCallback)(void *userdata, azaBuffer buffer);
typedef int (*fp_azaProcessDualCallback)(void *userdata, azaBuffer dst, azaBuffer src);


azaBuffer azaPushSideBuffer(uint32_t frames, uint32_t channels, uint32_t samplerate);

azaBuffer azaPushSideBufferZero(uint32_t frames, uint32_t channels, uint32_t samplerate);

azaBuffer azaPushSideBufferCopy(azaBuffer src);

void azaPopSideBuffer();

void azaPopSideBuffers(uint8_t count);



#ifndef AZA_METERS_MAX_CHANNELS
	#define AZA_METERS_MAX_CHANNELS 8
#endif
// Used for metering in the GUI
typedef struct azaMeters {
	// Up to AZA_METERS_MAX_CHANNELS channels of RMS metering (only updated when mixer GUI is open)
	float rmsSquaredAvg[AZA_METERS_MAX_CHANNELS];
	float peaks[AZA_METERS_MAX_CHANNELS];
	float peaksShortTerm[AZA_METERS_MAX_CHANNELS];
	// How many frames have been counted so far
	uint32_t rmsFrames;
	uint8_t activeMeters;
} azaMeters;
// Will update the meters with the entirety of the buffer's contents
void azaMetersUpdate(azaMeters *data, azaBuffer buffer, float inputAmp);



typedef enum azaDSPKind {
	AZA_DSP_NONE=0,
	// User-defined DSP that acts on a single buffer (transforming it in place)
	AZA_DSP_USER_SINGLE,
	// User-defined DSP that takes a src buffer and uses it to contribute to a dst buffer.
	// NOTE: Any azaDSPs in pNext will be called with the single-buffer interface on the dst buffer.
	AZA_DSP_USER_DUAL,
	AZA_DSP_CUBIC_LIMITER,
	AZA_DSP_RMS,
	AZA_DSP_LOOKAHEAD_LIMITER,
	AZA_DSP_FILTER,
	AZA_DSP_COMPRESSOR,
	AZA_DSP_DELAY,
	AZA_DSP_REVERB,
	AZA_DSP_SAMPLER,
	AZA_DSP_GATE,
	AZA_DSP_DELAY_DYNAMIC,
	AZA_DSP_SPATIALIZE,

	// The rest are passive monitors

	AZA_DSP_MONITOR_SPECTRUM,

	AZA_DSP_KIND_COUNT
} azaDSPKind;
extern const char *azaDSPKindString[];

// Generic interface to all the DSP structures
typedef struct azaDSP {
	azaDSPKind kind;
	/* struct {
		uint32_t structSize : 30;
		// This bit will be set if we were created with any of the azaMake... functions. As such, removal from a dsp chain will imply calling azaFree... on us as well.
		bool bypass : 1;
		bool owned : 1;
	} metadata;
	We do manual bit packing because god forbid our language spec gives us any guarantees or control over anything. */
	uint32_t metadata;
	struct azaDSP *pNext;
} azaDSP;
int azaDSPProcessSingle(azaDSP *data, azaBuffer buffer);
int azaDSPProcessDual(azaDSP *data, azaBuffer dst, azaBuffer src);
static inline uint32_t azaDSPPackMetadata(uint32_t structSize, bool bypass, bool owned) {
	assert(structSize < ((uint32_t)1 << 30));
	uint32_t result = structSize | ((uint32_t)bypass << 30) | ((uint32_t)owned << 31);
	return result;
}
static inline uint32_t azaDSPMetadataGetStructSize(uint32_t metadata) {
	return metadata & (((uint32_t)1 << 31) - 1);
}
static inline bool azaDSPMetadataGetBypass(uint32_t metadata) {
	return (metadata >> 30) & 1;
}
static inline void azaDSPMetadataSetBypass(uint32_t *metadata, bool bypass) {
	uint32_t bit = 1u << 30;
	*metadata = (*metadata & ~bit) | ((uint32_t)(!!bypass) << 30);
}
static inline void azaDSPMetadataToggleBypass(uint32_t *metadata) {
	uint32_t bit = 1u << 30;
	*metadata ^= bit;
}
static inline bool azaDSPMetadataGetOwned(uint32_t metadata) {
	return metadata >> 31;
}
static inline void azaDSPMetadataSetOwned(uint32_t *metadata) {
	*metadata |= (uint32_t)1 << 31;
}

// If you need to hold on to the name, copy it out as this pointer is only valid until the next call.
const char* azaGetDSPName(azaDSP *data);

// Will look for the dsp in the registry by name, and if it finds it, call the appropriate azaFree on it.
// Returns whether it was found and freed.
bool azaFreeDSP(azaDSP *dsp);



// Registry entry for existing DSP plugins, specifically for dynamically creating and destroying them from a GUI
typedef struct azaDSPRegEntry {
	char name[32];
	// Pointer to a function that makes a new azaDSP with a default configuration
	azaDSP* (*fp_makeDSP)(uint8_t channelCapInline);
	// Pointer to a function that frees an existing azaDSP
	void (*fp_freeDSP)(azaDSP *data);
} azaDSPRegEntry;
typedef struct azaDSPRegEntries {
	azaDSPRegEntry *data;
	uint32_t count;
	uint32_t capacity;
} azaDSPRegEntries;
extern azaDSPRegEntries azaDSPRegistry;
// May return AZA_ERROR_OUT_OF_MEMORY
int azaDSPAddRegEntry(const char *name, azaDSP* (*fp_makeDSP)(uint8_t), void (*fp_freeDSP)(azaDSP*));
// May return AZA_ERROR_OUT_OF_MEMORY
int azaDSPRegistryInit();


typedef struct azaDSPUser {
	azaDSP header;
	char name[32];
	void *userdata;
	union {
		fp_azaProcessCallback processSingle;
		fp_azaProcessDualCallback processDual;
	};
} azaDSPUser;
void azaDSPUserInitSingle(azaDSPUser *data, uint32_t allocSize, const char *name, void *userdata, fp_azaProcessCallback processCallback);
void azaDSPUserInitDual(azaDSPUser *data, uint32_t allocSize, const char *name, void *userdata, fp_azaProcessDualCallback processCallback);
int azaDSPUserProcessSingle(azaDSPUser *data, azaBuffer buffer);
int azaDSPUserProcessDual(azaDSPUser *data, azaBuffer dst, azaBuffer src);



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
	// Used in azaRMSProcessDual to combine all the channel values into a single RMS value per frame. If left NULL, defaults to azaOpMax
	fp_azaOp combineOp;
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

// returns the size in bytes of azaRMS with the given channelCapInline
uint32_t azaRMSGetAllocSize(azaRMSConfig config, uint8_t channelCapInline);
// initializes azaRMS in existing memory
void azaRMSInit(azaRMS *data, uint32_t allocSize, azaRMSConfig config, uint8_t channelCapInline);
// frees any additional memory that the azaRMS may have allocated
void azaRMSDeinit(azaRMS *data);

// Convenience function that allocates and inits an azaRMS for you
// May return NULL indicating an out-of-memory error
azaRMS* azaMakeRMS(azaRMSConfig config, uint8_t channelCapInline);
// Frees an azaRMS that was created with azaMakeRMS
void azaFreeRMS(azaRMS *data);

azaDSP* azaMakeDefaultRMS(uint8_t channelCapInline);

// Takes the rms of all the channels combined with config.combineOp and puts that into the first channel of dst
int azaRMSProcessDual(azaRMS *data, azaBuffer dst, azaBuffer src);
// Takes the rms of each channel individually
int azaRMSProcessSingle(azaRMS *data, azaBuffer buffer);



typedef azaDSP azaCubicLimiter;

// returns the size in bytes of azaCubicLimiter
static inline uint32_t azaCubicLimiterGetAllocSize() {
	return sizeof(azaCubicLimiter);
}
// initializes azaCubicLimiter in existing memory
void azaCubicLimiterInit(azaCubicLimiter *data, uint32_t allocSize);

// Convenience function that allocates and inits an azaCubicLimiter for you
// May return NULL indicating an out-of-memory error
azaCubicLimiter* azaMakeCubicLimiter();
// Frees an azaCubicLimiter that was created with azaMakeCubicLimiter
void azaFreeCubicLimiter(azaCubicLimiter *data);

azaDSP* azaMakeDefaultCubicLimiter(uint8_t);

int azaCubicLimiterProcess(azaCubicLimiter *data, azaBuffer buffer);



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

	azaMeters metersInput;
	azaMeters metersOutput;
	float minAmp, minAmpShort;

	// Data shared by all channels
	float peakBuffer[AZAUDIO_LOOKAHEAD_SAMPLES];
	int index;
	int cooldown;
	float sum;
	float slope;

	azaDSPChannelData channelData;
} azaLookaheadLimiter;

// returns the size in bytes of azaLookaheadLimiter with the given channelCapInline
uint32_t azaLookaheadLimiterGetAllocSize(uint8_t channelCapInline);
// initializes azaLookaheadLimiter in existing memory
void azaLookaheadLimiterInit(azaLookaheadLimiter *data, uint32_t allocSize, azaLookaheadLimiterConfig config, uint8_t channelCapInline);
// frees any additional memory that the azaLookaheadLimiter may have allocated
void azaLookaheadLimiterDeinit(azaLookaheadLimiter *data);

// Convenience function that allocates and inits an azaLookaheadLimiter for you
// May return NULL indicating an out-of-memory error
azaLookaheadLimiter* azaMakeLookaheadLimiter(azaLookaheadLimiterConfig config, uint8_t channelCapInline);
// Frees an azaLookaheadLimiter that was created with azaMakeLookaheadLimiter
void azaFreeLookaheadLimiter(azaLookaheadLimiter *data);

azaDSP* azaMakeDefaultLookaheadLimiter(uint8_t channelCapInline);

int azaLookaheadLimiterProcess(azaLookaheadLimiter *data, azaBuffer buffer);



typedef enum azaFilterKind {
	AZA_FILTER_HIGH_PASS,
	AZA_FILTER_LOW_PASS,
	AZA_FILTER_BAND_PASS,

	AZA_FILTER_KIND_COUNT
} azaFilterKind;
extern const char *azaFilterKindString[];

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
// returns the size in bytes of a filter with the given channelCapInline
uint32_t azaFilterGetAllocSize(uint8_t channelCapInline);
// initializes azaFilter in existing memory
void azaFilterInit(azaFilter *data, uint32_t allocSize, azaFilterConfig config, uint8_t channelCapInline);
// frees any additional memory that the azaFilter may have allocated
void azaFilterDeinit(azaFilter *data);

// Convenience function that allocates and inits an azaFilter for you
// May return NULL indicating an out-of-memory error
azaFilter* azaMakeFilter(azaFilterConfig config, uint8_t channelCapInline);
// Frees an azaFilter that was created with azaMakeFilter
void azaFreeFilter(azaFilter *data);

azaDSP* azaMakeDefaultFilter(uint8_t channelCapInline);

int azaFilterProcess(azaFilter *data, azaBuffer buffer);



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
	// output gain in dB
	float gain;
} azaCompressorConfig;

typedef struct azaCompressor {
	azaDSP header;
	azaCompressorConfig config;

	azaMeters metersInput;
	azaMeters metersOutput;

	float attenuation;
	float minGain, minGainShort;
	azaRMS rms;
} azaCompressor;

// returns the size in bytes of azaCompressor with the given channelCapInline
uint32_t azaCompressorGetAllocSize(uint8_t channelCapInline);
// initializes azaCompressor in existing memory
void azaCompressorInit(azaCompressor *data, uint32_t allocSize, azaCompressorConfig config, uint8_t channelCapInline);
// frees any additional memory that the azaCompressor may have allocated
void azaCompressorDeinit(azaCompressor *data);

// Convenience function that allocates and inits an azaCompressor for you
// May return NULL indicating an out-of-memory error
azaCompressor* azaMakeCompressor(azaCompressorConfig config, uint8_t channelCapInline);
// Frees an azaCompressor that was created with azaMakeCompressor
void azaFreeCompressor(azaCompressor *data);

azaDSP* azaMakeDefaultCompressor(uint8_t channelCapInline);

int azaCompressorProcess(azaCompressor *data, azaBuffer buffer);



typedef struct azaDelayConfig {
	// effect gain in dB
	float gain;
	// dry gain in dB
	float gainDry;
	bool muteWet, muteDry;
	// delay time in ms
	float delay;
	// 0 to 1 multiple of output feeding back into input
	float feedback;
	// How much of one channel's signal gets added to a different channel in the range 0 to 1
	float pingpong;
	// You can provide a chain of effects to operate on the wet output
	azaDSP *wetEffects;
} azaDelayConfig;

typedef struct azaDelayChannelConfig {
	// extra delay time in ms
	float delay;
} azaDelayChannelConfig;

typedef struct azaDelayChannelData {
	float *buffer;
	uint32_t delaySamples;
	uint32_t index;
	azaDelayChannelConfig config;
} azaDelayChannelData;

typedef struct azaDelay {
	azaDSP header;
	azaDelayConfig config;

	azaMeters metersInput;
	azaMeters metersOutput;

	// Combined big buffer that gets split for each channel
	float *buffer;
	uint32_t bufferCap;
	azaDSPChannelData channelData;
} azaDelay;

// returns the size in bytes of azaDelay with the given channelCapInline
uint32_t azaDelayGetAllocSize(uint8_t channelCapInline);
// initializes azaDelay in existing memory
void azaDelayInit(azaDelay *data, uint32_t allocSize, azaDelayConfig config, uint8_t channelCapInline);
// frees any additional memory that the azaDelay may have allocated
void azaDelayDeinit(azaDelay *data);

// Call this for each channel individually as they may not be contiguous in memory
azaDelayChannelConfig* azaDelayGetChannelConfig(azaDelay *data, uint8_t channel);

// Convenience function that allocates and inits an azaDelay for you
// May return NULL indicating an out-of-memory error
azaDelay* azaMakeDelay(azaDelayConfig config, uint8_t channelCapInline);
// Frees an azaDelay that was created with azaMakeDelay
void azaFreeDelay(azaDelay *data);

azaDSP* azaMakeDefaultDelay(uint8_t channelCapInline);

int azaDelayProcess(azaDelay *data, azaBuffer buffer);



typedef struct azaReverbConfig {
	// effect gain in dB
	float gain;
	// dry gain in dB
	float gainDry;
	bool muteWet, muteDry;
	// value affecting reverb feedback, roughly in the range of 1 to 100 for reasonable results
	float roomsize;
	// value affecting damping of high frequencies, roughly in the range of 1 to 5
	float color;
	// delay for first reflections in ms
	float delay;
} azaReverbConfig;

#define AZAUDIO_REVERB_DELAY_COUNT 30
typedef struct azaReverb {
	azaDSP header;
	azaReverbConfig config;
	azaDelay inputDelay;
	// azaDelay delays[AZAUDIO_REVERB_DELAY_COUNT];
	// azaFilter filters[AZAUDIO_REVERB_DELAY_COUNT];
} azaReverb;

// returns the size in bytes of azaReverb
uint32_t azaReverbGetAllocSize(uint8_t channelCapInline);
// initializes azaReverb in existing memory
void azaReverbInit(azaReverb *data, uint32_t allocSize, azaReverbConfig config, uint8_t channelCapInline);
// frees any additional memory that the azaReverb may have allocated
void azaReverbDeinit(azaReverb *data);

azaDelay* azaReverbGetDelayTap(azaReverb *data, int tap);
azaFilter* azaReverbGetFilterTap(azaReverb *data, int tap);

// Convenience function that allocates and inits an azaReverb for you
// May return NULL indicating an out-of-memory error
azaReverb* azaMakeReverb(azaReverbConfig config, uint8_t channelCapInline);
// Frees an azaReverb that was created with azaMakeReverb
void azaFreeReverb(azaReverb *data);

azaDSP* azaMakeDefaultReverb(uint8_t channelCapInline);

int azaReverbProcess(azaReverb *data, azaBuffer buffer);



#define AZAUDIO_SAMPLER_MAX_INSTANCES 32

typedef struct azaSamplerInstance {
	uint32_t id;
	int32_t frame;
	float fraction;
	bool reverse;
	azaADSRInstance envelope;
	azaFollowerLinear speed;
	azaFollowerLinear volume;
} azaSamplerInstance;

typedef struct azaSamplerConfig {
	// buffer containing the sound we're sampling
	azaBuffer *buffer;
	// If speed changes this is how long it takes to lerp to the new value in ms
	float speedTransitionTimeMs;
	// If gain changes this is how long it takes to lerp to the new value in ms (lerp happens in amp space)
	float volumeTransitionTimeMs;
	bool loop;
	bool pingpong;
	// Start of the looping region in frames
	// If this value is >= buffer->frames, we treat this value as 0
	int32_t loopStart;
	// End of the looping region in frames
	// If this value is <= loopStart, we treat this value as buffer->frames
	int32_t loopEnd;
	azaADSRConfig envelope;
} azaSamplerConfig;

typedef struct azaSampler {
	azaDSP header;
	azaSamplerConfig config;
	azaMutex mutex;

	azaSamplerInstance instances[AZAUDIO_SAMPLER_MAX_INSTANCES];
	uint8_t numInstances;
} azaSampler;

// returns the size in bytes of azaSampler (included for completeness)
static inline uint32_t azaSamplerGetAllocSize() {
	return (uint32_t)sizeof(azaSampler);
}
// initializes azaSampler in existing memory
void azaSamplerInit(azaSampler *data, uint32_t allocSize, azaSamplerConfig config);
// frees any additional memory that the azaSampler may have allocated
void azaSamplerDeinit(azaSampler *data);

// Convenience function that allocates and inits an azaSampler for you
// May return NULL indicating an out-of-memory error
azaSampler* azaMakeSampler(azaSamplerConfig config);
// Frees an azaSampler that was created with azaMakeSampler
void azaFreeSampler(azaSampler *data);

azaDSP* azaMakeDefaultSampler(uint8_t);

int azaSamplerProcess(azaSampler *data, azaBuffer buffer);

// Adds an instance of the sound
// speed affects the rate of playback for this instance (pitch control where 1.0f is base speed)
// gainDB affects the volume for this instance
// returns the sound id, used for interacting with this instance later
uint32_t azaSamplerPlay(azaSampler *data, float speed, float gainDB);

// may return NULL, indicating the id wasn't found
azaSamplerInstance* azaSamplerGetInstance(azaSampler *data, uint32_t id);

static inline void azaSamplerSetSpeed(azaSampler *data, uint32_t id, float speed) {
	azaMutexLock(&data->mutex);
	azaSamplerInstance *instance = azaSamplerGetInstance(data, id);
	if (instance) {
		azaFollowerLinearSetTarget(&instance->speed, speed);
	}
	azaMutexUnlock(&data->mutex);
}

static inline void azaSamplerSetGain(azaSampler *data, uint32_t id, float gainDB) {
	azaMutexLock(&data->mutex);
	azaSamplerInstance *instance = azaSamplerGetInstance(data, id);
	if (instance) {
		azaFollowerLinearSetTarget(&instance->volume, aza_db_to_ampf(gainDB));
	}
	azaMutexUnlock(&data->mutex);
}

static inline float azaSamplerGetSpeedCurrent(azaSampler *data, uint32_t id) {
	azaMutexLock(&data->mutex);
	azaSamplerInstance *instance = azaSamplerGetInstance(data, id);
	float result = 0.0f;
	if (instance) {
		result = azaFollowerLinearGetValue(&instance->speed);
	}
	azaMutexUnlock(&data->mutex);
	return result;
}

static inline float azaSamplerGetGainCurrent(azaSampler *data, uint32_t id) {
	azaMutexLock(&data->mutex);
	azaSamplerInstance *instance = azaSamplerGetInstance(data, id);
	float result = 0.0f;
	if (instance) {
		result = aza_amp_to_dbf(azaFollowerLinearGetValue(&instance->volume));
	}
	azaMutexUnlock(&data->mutex);
	return result;
}

static inline float azaSamplerGetSpeedTarget(azaSampler *data, uint32_t id) {
	azaMutexLock(&data->mutex);
	azaSamplerInstance *instance = azaSamplerGetInstance(data, id);
	float result = 0.0f;
	if (instance) {
		result = instance->speed.end;
	}
	azaMutexUnlock(&data->mutex);
	return result;
}

static inline float azaSamplerGetGainTarget(azaSampler *data, uint32_t id) {
	azaMutexLock(&data->mutex);
	azaSamplerInstance *instance = azaSamplerGetInstance(data, id);
	float result = 0.0f;
	if (instance) {
		result = aza_amp_to_dbf(instance->volume.end);
	}
	azaMutexUnlock(&data->mutex);
	return result;
}

// Triggers the release of the given sound instance
void azaSamplerStop(azaSampler *data, uint32_t id);

void azaSamplerStopAll(azaSampler *data);



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
	float attenuation;
	float gain;
	azaRMS rms;
} azaGate;

// returns the size in bytes of azaGate with the given channelCapInline
uint32_t azaGateGetAllocSize();
// initializes azaGate in existing memory
void azaGateInit(azaGate *data, uint32_t allocSize, azaGateConfig config);
// frees any additional memory that the azaGate may have allocated
void azaGateDeinit(azaGate *data);

// Convenience function that allocates and inits an azaGate for you
// May return NULL indicating an out-of-memory error
azaGate* azaMakeGate(azaGateConfig config);
// Frees an azaGate that was created with azaMakeGate
void azaFreeGate(azaGate *data);

azaDSP* azaMakeDefaultGate(uint8_t);

int azaGateProcess(azaGate *data, azaBuffer buffer);



typedef struct azaDelayDynamicConfig {
	// effect gain in dB
	float gain;
	// dry gain in dB
	float gainDry;
	// max possible delay in ms
	// If you increase this it will grow the buffer, filling the empty space with zeroes
	float delayMax;
	// 0 to 1 multiple of output feeding back into input
	float feedback;
	// How much of one channel's signal gets added to a different channel in the range 0 to 1
	float pingpong;
	// You can provide a chain of effects to operate on the wet input
	azaDSP *wetEffects;
	// Resampling kernel. If NULL it will use azaKernelDefaultLanczos
	struct azaKernel *kernel;
} azaDelayDynamicConfig;

typedef struct azaDelayDynamicChannelConfig {
	// delay in ms
	float delay;
} azaDelayDynamicChannelConfig;

typedef struct azaDelayDynamicChannelData {
	float *buffer;
	// Calculate this when processing
	// float delaySamples;
	// float index;
	azaDelayDynamicChannelConfig config;
} azaDelayDynamicChannelData;

typedef struct azaDelayDynamic {
	azaDSP header;
	azaDelayDynamicConfig config;
	// Combined big buffer that gets split for each channel
	float *buffer;
	uint32_t bufferCap;
	uint32_t lastSrcBufferFrames;
	azaDSPChannelData channelData;
} azaDelayDynamic;

// returns the size in bytes of azaDelayDynamic with the given channelCapInline
uint32_t azaDelayDynamicGetAllocSize(uint8_t channelCapInline);
// initializes azaDelayDynamic in existing memory
// channelConfigs can be NULL, which indicates for them to be zeroed out, otherwise expects an array of size channelCount
// Will allocate additional memory if channelCount > channelCapInline, and as such can return AZA_ERROR_OUT_OF_MEMORY. If you know that channelCapInline >= channelCount then you can safely ignore the return value.
int azaDelayDynamicInit(azaDelayDynamic *data, uint32_t allocSize, azaDelayDynamicConfig config, uint8_t channelCapInline, uint8_t channelCount, azaDelayDynamicChannelConfig *channelConfigs);
// frees any additional memory that the azaDelayDynamic may have allocated
void azaDelayDynamicDeinit(azaDelayDynamic *data);

// Call this for each channel individually as they may not be contiguous in memory
azaDelayDynamicChannelConfig* azaDelayDynamicGetChannelConfig(azaDelayDynamic *data, uint8_t channel);

// Convenience function that allocates and inits an azaDelayDynamic for you
// channelConfigs can be NULL, which indicates for them to be zeroed out
// May return NULL indicating an out-of-memory error
azaDelayDynamic* azaMakeDelayDynamic(azaDelayDynamicConfig config, uint8_t channelCapInline, uint8_t channelCount, azaDelayDynamicChannelConfig *channelConfigs);
// Frees an azaDelayDynamic that was created with azaMakeDelayDynamic
void azaFreeDelayDynamic(azaDelayDynamic *data);

azaDSP* azaMakeDefaultDelayDynamic(uint8_t channelCapInline);

// if endChannelDelays is not NULL, then over the length of the buffer each channel's delay will lerp towards its respective endChannelDelay, and finally set the value in the channel config once it's done processing.
int azaDelayDynamicProcess(azaDelayDynamic *data, azaBuffer buffer, float *endChannelDelays);



typedef struct azaKernel {
	// length of the kernel (in samples)
	uint32_t length;
	// which sample along our length represents a time offset of zero
	uint32_t sampleZero;
	// How many sub-samples there are between each sample
	uint32_t scale;
	// total size of table, which is length * scale
	uint32_t size;
	// Standard layout where kernel samples are in order. This is where you write the kernel before calling azaKernelPack.
	float *table;
	// An alternate layout of the table optimized for sampling
	// The following is a semantic representation of the layout:
	// struct {
	// 	float samples[length];
	// } subsamples[scale];
	float *packed;
} azaKernel;

extern azaKernel azaKernelDefaultLanczos;

// Creates a blank kernel
// Will allocate memory for the table (may return AZA_ERROR_OUT_OF_MEMORY)
int azaKernelInit(azaKernel *kernel, uint32_t length, uint32_t sampleZero, uint32_t scale);
void azaKernelDeinit(azaKernel *kernel);

// Must be called after kernel->table is populated, and before using the kernel for any sampling.
void azaKernelPack(azaKernel *kernel);

float azaKernelSample(azaKernel *kernel, int i, float pos);

float azaSampleWithKernel(float *src, int stride, int minFrame, int maxFrame, azaKernel *kernel, float pos);

// Makes a lanczos kernel. resolution is the number of samples between zero crossings
// May return AZA_ERROR_OUT_OF_MEMORY
int azaKernelMakeLanczos(azaKernel *kernel, uint32_t resolution, uint32_t radius);

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
#define AZA_WORLD_DEFAULT ((azaWorld*)0ull)


typedef enum azaSpatializeMode {
	AZA_SPATIALIZE_SIMPLE,
	AZA_SPATIALIZE_ADVANCED,
} azaSpatializeMode;

typedef struct azaSpatializeConfig {
	// if world is NULL, it will use azaWorldDefault
	const azaWorld *world;
	// TODO: We probably want the doppler effect to be separate from what kind of spatialization we want.
	// AZA_SPATIALIZE_SIMPLE will only do volume-based spatialization
	// AZA_SPATIALIZE_ADVANCED will also do a per-channel doppler effect, which is ideal for headphones.
	azaSpatializeMode mode;
	// Maximum delay time in ms for ADVANCED mode. If this is zero, we'll use some default that should work for most reasonable distances.
	float delayMax;
	// In ADVANCED mode, this specifies how far each channel is from the origin in their respective directions. Used to calculate per-channel delays. If this is zero, it will default to 0.085f (half of the average human head width).
	float earDistance;
} azaSpatializeConfig;

typedef struct azaSpatializeChannelData {
	azaFilter filter;
} azaSpatializeChannelData;

typedef struct azaSpatialize {
	azaDSP header;
	azaSpatializeConfig config;
	azaDSPChannelData channelData;
	// azaDelayDynamic delay;
} azaSpatialize;

// returns the size in bytes of azaSpatialize with the given channelCapInline
uint32_t azaSpatializeGetAllocSize(uint8_t channelCapInline);
// initializes azaSpatialize in existing memory
void azaSpatializeInit(azaSpatialize *data, uint32_t allocSize, azaSpatializeConfig config, uint8_t channelCapInline);
// frees any additional memory that the azaSpatialize may have allocated
void azaSpatializeDeinit(azaSpatialize *data);

azaDelayDynamic* azaSpatializeGetDelayDynamic(azaSpatialize *data);

// Convenience function that allocates and inits an azaSpatialize for you
// May return NULL indicating an out-of-memory error
azaSpatialize* azaMakeSpatialize(azaSpatializeConfig config, uint8_t channelCapInline);
// Frees an azaSpatialize that was created with azaMakeSpatialize
void azaFreeSpatialize(azaSpatialize *data);

// Adds its sound to the existing signal in dstBuffer.
// dstBuffer and srcBuffer must have the same number of frames and the same samplerate.
// srcBuffer MUST be 1-channel
// Doesn't attenuate the volume by distance. You must do that yourself and pass the result into srcAmp.
int azaSpatializeProcess(azaSpatialize *data, azaBuffer dstBuffer, azaBuffer srcBuffer, azaVec3 srcPosStart, float srcAmpStart, azaVec3 srcPosEnd, float srcAmpEnd);



typedef enum azaMonitorSpectrumMode {
	// Reports the spectrum of a single chosen channel
	AZA_MONITOR_SPECTRUM_MODE_ONE_CHANNEL,
	// Reports the average spectrum from all channels
	AZA_MONITOR_SPECTRUM_MODE_AVG_CHANNELS,
	AZA_MONITOR_SPECTRUM_MODE_COUNT,
} azaMonitorSpectrumMode;

typedef struct azaMonitorSpectrumConfig {
	azaMonitorSpectrumMode mode;
	// In AZA_MONITOR_SPECTRUM_MODE_ONE_CHANNEL mode, this is which channel gets analyzed
	uint8_t channelChosen;
	// FFT window size (must be a power of 2)
	uint16_t window;
	// How much of the existing signal is kept every time the output is updated.
	// Ex: out = lerp(out, in, 1/(1+smoothing));
	uint16_t smoothing;
} azaMonitorSpectrumConfig;

typedef struct azaMonitorSpectrum {
	azaDSP header;
	azaMonitorSpectrumConfig config;

	uint32_t samplerate;

	float *inputBuffer;
	uint32_t inputBufferCapacity;
	uint32_t inputBufferUsed;
	uint8_t inputBufferChannelCount;

	uint16_t numCounted;
	uint32_t outputBufferCapacity;
	float *outputBuffer;
} azaMonitorSpectrum;

inline uint32_t azaMonitorSpectrumGetAllocSize() {
	return sizeof(azaMonitorSpectrum);
}
// initializes azaMonitorSpectrum in existing memory
void azaMonitorSpectrumInit(azaMonitorSpectrum *data, azaMonitorSpectrumConfig config);
// frees any additional memory that the azaMonitorSpectrum may have allocated
void azaMonitorSpectrumDeinit(azaMonitorSpectrum *data);
// Convenience function that allocates and inits an azaMonitorSpectrum for you
// May return NULL indicating an out-of-memory error
azaMonitorSpectrum* azaMakeMonitorSpectrum(azaMonitorSpectrumConfig config);
// Frees an azaMonitorSpectrum that was created with azaMakeMonitorSpectrum
void azaFreeMonitorSpectrum(azaMonitorSpectrum *data);

azaDSP* azaMakeDefaultMonitorSpectrum(uint8_t channelCountInline);

int azaMonitorSpectrumProcess(azaMonitorSpectrum *data, azaBuffer buffer);


#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_DSP_H