/*
	File: azaDSP.h
	Author: Philip Haynes
	Generic interface to all the DSP plugin structures.
*/

#ifndef AZAUDIO_AZADSP_H
#define AZAUDIO_AZADSP_H

#include "azaBuffer.h"

#ifdef __cplusplus
extern "C" {
#endif



// Some specs used to help manage buffers, especially in the mixer.
// Relies heavily on ZII for correctness
typedef struct azaDSPSpecs {
	// How many samples of latency does the plugin create in the chain? Used for latency compensation.
	uint32_t latencyFrames;
	// How many src leading frames are desired for processing. Used for kernel sampling.
	uint32_t leadingFrames;
	// How many src trailing frames are desired for processing. Used for kernel sampling.
	uint32_t trailingFrames;
} azaDSPSpecs;

// Combines specs for plugins that run in series (where one's output gets fed into the next one's input)
void azaDSPSpecsCombineSerial(azaDSPSpecs *dst, azaDSPSpecs *src);
// Combines specs for plugins that run in parallel (multiple independent plugins that input and output in the same places)
void azaDSPSpecsCombineParallel(azaDSPSpecs *dst, azaDSPSpecs *src);

// dsp is a pointer to the azaDSP derivative.
typedef azaDSPSpecs (*fp_azaDSPGetSpecs)(void *dsp, uint32_t samplerate);

// Flags passed into azaDSP.fp_process
enum {
	AZA_DSP_PROCESS_FLAG_CUT = 1, // A discontinuity occurred in the src azaBuffer since the last time we processed. This may indicate the plugin was moved from one chain to another, so persistent state should be reset.
};
// dsp is a pointer to the azaDSP derivative that contains all the metadata and configuration
// dst is the buffer to be written to during processing
// src is a buffer that acts as an input for processing
// The data in dst and src are allowed to overlap (they may even be the exact same buffer), so reading from src must happen before writing to dst (may want to copy src to a sideBuffer).
// flags enable the ability for process to handle a handful of events relevant to config and plugin chain changes
// may return error codes depending on the kind of plugin
typedef int (*fp_azaDSPProcess_t)(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);
// dsp is a pointer to the azaDSP derivative. This function must free all additional memory that was allocated by the plugin, and then the struct itself.
typedef void (*fp_azaFreeDSP_t)(void *dsp);

// TODO: Possibly do a little work to handle processing errors gracefully, such that an error caused by configuration in the GUI is easily recoverable by simply changing the config again (and maybe un-bypassing manually?).

// Must be at the start of actual plugins
typedef struct azaDSP {
	uint32_t size; // Total size of the azaDSP derivative struct (including all config and channeldata).
	uint8_t version; // Version of derived plugin, for use after AzAudio 1.0, and only for backwards-compatible changes.
	bool owned; // If true, upon removal from a plugin chain via the mixer GUI we call fp_free.
	bool bypass; // If true, our DSP doesn't get processed and instead skips to the next in the list.
	uint8_t selected; // Bitset for being selected in the mixer GUI (each bit represents a different view).
	uint8_t prevChannelCountDst; // How many dst channels were in the last azaDSPProcess call, used for handling changes.
	uint8_t prevChannelCountSrc; // How many src channels were in the last azaDSPProcess call, used for handling changes.
	byte _reserved[2]; // Explicit padding bytes reserved for later.
	int32_t error; // If nonzero, there was an error when processing, so disable this plugin until the user requests to try again. Stores the error code.
	char name[32]; // Null-terminated string. Unused chars should be zeroed.
	fp_azaDSPGetSpecs fp_getSpecs; // Nullable, meaning a zeroed-out struct azaDSPSpecs.
	fp_azaDSPProcess_t fp_process; // Nullable, meaning no processing is required.
	fp_azaFreeDSP_t fp_free; // Nullable, meaning removal from a plugin chain requires no action (mostly for un-owned user plugins).
	// TODO: function for drawing in the GUI
	// A doubly-linked list of plugins representing a single processing chain.
	struct azaDSP *pPrev;
	struct azaDSP *pNext;
} azaDSP;

static_assert(sizeof(azaDSP) == (48 + sizeof(void*)*5), "Please update the expected size of azaDSP and remember to reserve padding explicitly.");

// Handles bypass and calls dsp->fp_getSpecs if applicable.
azaDSPSpecs azaDSPGetSpecs(azaDSP *dsp, uint32_t samplerate);
// returns the combined azaDSPSpecs of the plugin chain up to and including dsp.
azaDSPSpecs azaDSPChainGetSpecsBackward(azaDSP *dsp, uint32_t samplerate);
// returns the combined azaDSPSpecs of the plugin chain from dsp until the end.
azaDSPSpecs azaDSPChainGetSpecsForward(azaDSP *dsp, uint32_t samplerate);
// returns the combined azaDSPSpecs of the entire plugin chain.
azaDSPSpecs azaDSPChainGetSpecs(azaDSP *dsp, uint32_t samplerate);
// Handles bypass, and calls dsp->fp_process if applicable.
// NOTE: Does not follow the plugin chain. You must do that externally.
int azaDSPProcess(azaDSP *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);
// Handles owned, and calls dsp->fp_free if applicable. Returns true if dsp was freed.
bool azaFreeDSP(azaDSP *dsp);



// Registry entry for existing DSP plugins, specifically for dynamically creating and destroying them from a GUI



typedef struct azaDSPRegEntry {
	// All the basic information about this kind of plugin lives in azaDSP, so just use that here.
	azaDSP base;
	// Pointer to a function that makes a new azaDSP with a default configuration
	azaDSP* (*fp_makeDSP)();
} azaDSPRegEntry;

typedef struct azaDSPRegEntries {
	azaDSPRegEntry *data;
	uint32_t count;
	uint32_t capacity;
} azaDSPRegEntries;

extern azaDSPRegEntries azaDSPRegistry;

// May return AZA_ERROR_OUT_OF_MEMORY
int azaDSPRegistryInit();
// May return AZA_ERROR_OUT_OF_MEMORY
int azaDSPAddRegEntry(azaDSP base, azaDSP* (*fp_makeDSP)());



// Parameters for DSP



typedef void (*fp_azaOp)(float *lhs, float rhs);
void azaOpAdd(float *lhs, float rhs);
void azaOpMax(float *lhs, float rhs);



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZADSP_H