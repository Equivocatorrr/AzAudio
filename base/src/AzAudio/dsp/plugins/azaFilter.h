/*
	File: azaFilter.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_AZAFILTER_H
#define AZAUDIO_AZAFILTER_H

#include "../azaDSP.h"
#include "../azaMeters.h"
#include "../utility.h"

#ifdef __cplusplus
extern "C" {
#endif



#define AZAUDIO_FILTER_MAX_POLES 16

// Constants to define number of filter poles in terms of db/octave
enum {
	AZA_FILTER_6_DB=0,
	AZA_FILTER_12_DB,
	AZA_FILTER_18_DB,
	AZA_FILTER_24_DB,
	AZA_FILTER_30_DB,
	AZA_FILTER_36_DB,
	AZA_FILTER_42_DB,
	AZA_FILTER_48_DB,
	AZA_FILTER_54_DB,
	AZA_FILTER_60_DB,
	AZA_FILTER_66_DB,
	AZA_FILTER_72_DB,
	AZA_FILTER_78_DB,
	AZA_FILTER_84_DB,
	AZA_FILTER_90_DB,
	AZA_FILTER_96_DB,
};

typedef enum azaFilterKind {
	AZA_FILTER_HIGH_PASS,
	AZA_FILTER_LOW_PASS,
	AZA_FILTER_BAND_PASS,

	AZA_FILTER_KIND_COUNT
} azaFilterKind;
extern const char *azaFilterKindString[];

typedef struct azaFilterConfig {
	azaFilterKind kind;
	// pole count - 1 (defaults to AZA_FILTER_6_DB)
	uint32_t poles;
	// Cutoff frequency in Hz
	float frequency;
	// Blends the effect output with the dry signal where 1 is fully dry and 0 is fully wet.
	float dryMix;
	// Additional wet gain in dB
	float gainWet;
	// How long it takes to linear fade into the target frequency
	float frequencyFollowTime_ms;
	// As long as these are 0.0f, we use the frequency above, otherwise you can specify a different frequency on a per-channel basis.
	float channelFrequencyOverride[AZA_MAX_CHANNEL_POSITIONS];
} azaFilterConfig;

typedef struct azaFilterChannelData {
	// Only used if channelFrequencyOverrides are used
	azaFollowerLinear frequency;
	float outputs[2*AZAUDIO_FILTER_MAX_POLES];
} azaFilterChannelData;

typedef struct azaFilter {
	azaDSP header;
	azaFilterConfig config;

	azaMeters metersInput;
	azaMeters metersOutput;

	azaFollowerLinear frequency;

	azaFilterChannelData channelData[AZA_MAX_CHANNEL_POSITIONS];
} azaFilter;

// initializes azaFilter in existing memory
void azaFilterInit(azaFilter *data, azaFilterConfig config);
// frees any additional memory that the azaFilter may have allocated
void azaFilterDeinit(azaFilter *data);
// Resets state. May be called automatically.
void azaFilterReset(azaFilter *data);
// Resets state for the specified channel range
void azaFilterResetChannels(azaFilter *data, uint32_t firstChannel, uint32_t channelCount);

// Convenience function that allocates and inits an azaFilter for you
// May return NULL indicating an out-of-memory error
azaFilter* azaMakeFilter(azaFilterConfig config);
// Frees an azaFilter that was created with azaMakeFilter
void azaFreeFilter(void *dsp);

azaDSP* azaMakeDefaultFilter();

int azaFilterProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);



static const azaDSP azaFilterHeader = {
	/* .size         = */ sizeof(azaFilter),
	/* .version      = */ 1,
	/* .owned, bypass, selected, prevChannelCountDst, prevChannelCountSrc */ false, false, false, 0, 0,
	/* ._reserved    = */ {0},
	/* .error        = */ 0,
	/* .name         = */ "Filter",
	/* fp_getSpecs   = */ NULL, // As an IIR filter, we affect the phase, which depends on the frequency, so we report zero latency.
	/* fp_process    = */ azaFilterProcess,
	/* fp_free       = */ azaFreeFilter,
};



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZAFILTER_H
