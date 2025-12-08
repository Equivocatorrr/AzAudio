/*
	File: azaDSPMultiplexer.h
	Author: Philip Haynes
	Utility that has an azaDSPChain as a template which then can instantiate copies of that chain that get run in parallel with their outputs added together in the dst buffer
*/

#ifndef AZAUDIO_AZADSPMULTIPLEXER_H
#define AZAUDIO_AZADSPMULTIPLEXER_H

#include "../azaDSP.h"
#include "../../backend/threads.h"

#ifdef __cplusplus
extern "C" {
#endif



extern const azaDSP azaDSPMultiplexerHeader;

typedef struct azaDSPMultiplexerInstance {
	azaDSPChain chain;
	uint32_t id;
	bool initted;
	bool active;
	aza_byte _reserved[2];
} azaDSPMultiplexerInstance;

typedef struct azaDSPMultiplexer {
	azaDSP dsp;
	// A DSP Chain that never gets processed itself, but instead hosts plugin configs that get reflected in all of the instances
	azaDSPChain origin;
	azaMutex mutex;
	struct {
		azaDSPMultiplexerInstance *data;
		uint32_t count;
		uint32_t capacity;
	} instances;
} azaDSPMultiplexer;

// initializes azaDSPMultiplexer in existing memory
void azaDSPMultiplexerInit(azaDSPMultiplexer *data);
// frees any additional memory that the azaDSPMultiplexer may have allocated
void azaDSPMultiplexerDeinit(azaDSPMultiplexer *data);

// Convenience function that allocates and inits an azaDSPMultiplexer for you
// May return NULL indicating an out-of-memory error
azaDSPMultiplexer* azaDSPMultiplexerMake();
// Frees an azaDSPMultiplexer that was created with azaDSPMultiplexerMake
void azaDSPMultiplexerFree(azaDSP *dsp);

azaDSP* azaDSPMultiplexerMakeDefault();
azaDSP* azaDSPMultiplexerMakeDuplicate(azaDSP *src);
int azaDSPMultiplexerCopyConfig(azaDSP *dst, azaDSP *src);

int azaDSPMultiplexerProcess(azaDSP *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags);

azaDSPSpecs azaDSPMultiplexerGetSpecs(azaDSP *dsp, uint32_t samplerate);


#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_AZADSPMULTIPLEXER_H
