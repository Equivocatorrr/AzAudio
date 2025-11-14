/*
	File: azaDSPDebugger.h
	Author: Philip Haynes
	A plugin with some config settings for debugging plugin chains.
*/

#ifndef AZAUDIO_AZADSPDEBUGGER_H
#define AZAUDIO_AZADSPDEBUGGER_H

#include "../azaDSP.h"

#ifdef __cplusplus
extern "C" {
#endif



typedef struct azaDSPDebuggerConfig {
	azaDSPSpecs specsToReport;
} azaDSPDebuggerConfig;

typedef struct azaDSPDebugger {
	azaDSP header;
	azaDSPDebuggerConfig config;
} azaDSPDebugger;

// initializes azaDSPDebugger in existing memory
void azaDSPDebuggerInit(azaDSPDebugger *data, azaDSPDebuggerConfig config);
// frees any additional memory that the azaDSPDebugger may have allocated
void azaDSPDebuggerDeinit(azaDSPDebugger *data);
// Resets state. May be called automatically.
void azaDSPDebuggerReset(azaDSPDebugger *data);
// Resets state for the specified channel range
void azaDSPDebuggerResetChannels(azaDSPDebugger *data, uint32_t firstChannel, uint32_t channelCount);

// Convenience function that allocates and inits an azaDSPDebugger for you
// May return NULL indicating an out-of-memory error
azaDSPDebugger* azaMakeDSPDebugger(azaDSPDebuggerConfig config);
// Frees an azaDSPDebugger that was created with azaMakeDSPDebugger
void azaFreeDSPDebugger(void *dsp);

azaDSP* azaMakeDefaultDSPDebugger();

int azaDSPDebuggerProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);

azaDSPSpecs azaDSPDebuggerGetSpecs(void *dsp, uint32_t samplerate);



void azagDrawDSPDebugger(void *dsp, azagRect bounds);



static const azaDSP azaDSPDebuggerHeader = {
	/* .size         = */ sizeof(azaDSPDebugger),
	/* .version      = */ 1,
	/* .owned, bypass, selected, prevChannelCountDst, prevChannelCountSrc */ false, false, false, 0, 0,
	/* ._reserved    = */ {0},
	/* .error        = */ 0,
	/* .name         = */ "DSP Debugger",
	/* fp_getSpecs   = */ azaDSPDebuggerGetSpecs,
	/* fp_process    = */ azaDSPDebuggerProcess,
	/* fp_free       = */ azaFreeDSPDebugger,
	/* fp_draw       = */ azagDrawDSPDebugger,
};



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZADSPDEBUGGER_H
