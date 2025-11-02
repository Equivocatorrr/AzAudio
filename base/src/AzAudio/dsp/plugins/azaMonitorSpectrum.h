/*
	File: azaMonitorSpectrum.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_AZAMONITORSPECTRUM_H
#define AZAUDIO_AZAMONITORSPECTRUM_H

#include "../azaDSP.h"

#ifdef __cplusplus
extern "C" {
#endif



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
	// If false, we shift our input over by half a window each update instead of the whole way, effectively doubling our update rate and giving samples on the edge of the window the opportunity to contribute.
	bool fullWindowProgression;
	// FFT window size (must be a power of 2)
	uint16_t window;
	// How much of the existing signal is kept every time the output is updated.
	// Ex: out = lerp(out, in, 1/(1+smoothing));
	uint16_t smoothing;
	// Minimum dB level to show on the graph in the mixer gui.
	// It's recommended to set this well below zero for most use-cases.
	// -96dB is the ULP for 16-bit audio
	// -144dB is the ULP for 24-bit audio
	int16_t floor;
	// Maximum dB level to show on the graph in the mixer gui.
	// 0dB works for sounds that generally don't peak above 0dB, otherwise having a little headroom could be nice.
	int16_t ceiling;
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

// initializes azaMonitorSpectrum in existing memory
void azaMonitorSpectrumInit(azaMonitorSpectrum *data, azaMonitorSpectrumConfig config);
// frees any additional memory that the azaMonitorSpectrum may have allocated
void azaMonitorSpectrumDeinit(azaMonitorSpectrum *data);
// Resets state. May be called automatically.
void azaMonitorSpectrumReset(azaMonitorSpectrum *data);
// Resets state for the specified channel range
void azaMonitorSpectrumResetChannels(azaMonitorSpectrum *data, uint32_t firstChannel, uint32_t channelCount);

// Convenience function that allocates and inits an azaMonitorSpectrum for you
// May return NULL indicating an out-of-memory error
azaMonitorSpectrum* azaMakeMonitorSpectrum(azaMonitorSpectrumConfig config);
// Frees an azaMonitorSpectrum that was created with azaMakeMonitorSpectrum
void azaFreeMonitorSpectrum(void *dsp);

azaDSP* azaMakeDefaultMonitorSpectrum();

int azaMonitorSpectrumProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);



static const azaDSP azaMonitorSpectrumHeader = {
	/* .size         = */ sizeof(azaMonitorSpectrum),
	/* .version      = */ 1,
	/* .owned, bypass, selected, prevChannelCountDst, prevChannelCountSrc */ false, false, false, 0, 0,
	/* ._reserved    = */ {0},
	/* .error        = */ 0,
	/* .name         = */ "MonitorSpectrum",
	/* fp_getSpecs   = */ NULL,
	/* fp_process    = */ azaMonitorSpectrumProcess,
	/* fp_free       = */ azaFreeMonitorSpectrum,
};



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZAMONITORSPECTRUM_H
