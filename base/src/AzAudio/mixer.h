/*
	File: mixer.h
	Author: Philip Haynes
	General purpose mixer with track routing and DSP plugins.
*/

#ifndef AZAUDIO_MIXING_H
#define AZAUDIO_MIXING_H

#include "dsp.h"
#include "backend/interface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct azaTrackRoute {
	struct azaTrack *track;
	float gain;
	azaChannelMatrix channelMatrix;
} azaTrackRoute;

// a track has the capabilities of a bus and can have sound sources on it
typedef struct azaTrack {
	azaBuffer buffer;
	// Plugin chain, including synths and samplers
	azaDSP *dsp;
	struct {
		azaTrackRoute *data;
		uint32_t count;
		uint32_t capacity;
	} receives;
	// Used to determine whether routing is cyclic.
	uint8_t mark;
} azaTrack;
// Initializes our buffer
// May return any error azaBufferInit can return
int azaTrackInit(azaTrack *data, uint32_t bufferFrames, azaChannelLayout bufferChannelLayout);
void azaTrackDeinit(azaTrack *data);
// Adds a dsp to the end of the dsp chain
void azaTrackAppendDSP(azaTrack *data, azaDSP *dsp);
// Adds a dsp to the beginning of the dsp chain
void azaTrackPrependDSP(azaTrack *data, azaDSP *dsp);

enum {
	// Tells azaTrackConnect not to generate any default values for the channelMatrix (leaving them all at zero)
	AZA_TRACK_CHANNEL_ROUTING_ZERO = 0x0001,
};

// Routes the output of from to the input of to (bet you had to reread that a few times)
// if dstTrackRoute is not NULL, this outputs the connection that was just made (or the existing one). This pointer is only guaranteed to be valid until the next time you call this function, so don't hold on to it)
// May return AZA_ERROR_OUT_OF_MEMORY
int azaTrackConnect(azaTrack *from, azaTrack *to, float gain, azaTrackRoute **dstTrackRoute, uint32_t flags);
void azaTrackDisconnect(azaTrack *from, azaTrack *to);

int azaTrackProcess(uint32_t frames, uint32_t samplerate, azaTrack *data);

typedef struct azaMixerConfig {
	uint32_t bufferFrames;
	uint32_t trackCount;
	// Can be NULL to default to master channel layout, otherwise must point to an array of trackCount layouts, one for each track.
	azaChannelLayout *channelLayouts;
} azaMixerConfig;

typedef struct azaMixer {
	azaMixerConfig config;
	azaTrack *tracks;
	azaTrack master;
	// We may optionally own a stream to which we output the track contents of master.
	azaStream stream;
} azaMixer;

// Allocates config.trackCount tracks and initializes them
// config.bufferFrames indicates how many frames our buffers should have. This should probably match the maximum size of the backend buffer, if applicable.
// masterChannelLayout will be used to initialize the master track's buffer channel layout, and also all other track channel layouts if config.channelLayouts is NULL or if the channel's respective channelLayout has 0 channels
// May return AZA_ERROR_OUT_OF_MEMORY if we failed to allocate tracks, or any error azaBufferInit can return
int azaMixerInit(azaMixer *data, azaMixerConfig config, azaChannelLayout masterChannelLayout);
void azaMixerDeinit(azaMixer *data);
// Processes all the tracks to produce a result into the output track.
// frames MUST be <= data->config.bufferFrames
int azaMixerProcess(uint32_t frames, uint32_t samplerate, azaMixer *data);

// Builtin callback for processing the mixer on a stream
int azaMixerCallback(void *userdata, azaBuffer buffer);

// Opens an output stream to process this mixer and initializes it such that the tracks have enough frames.
// config.bufferFrames is set to the max of the value passed in or the number required for the output stream. As such you can leave this at zero.
// if activate is true then this call will also start the stream immediately without you needing to call azaMixerStreamSetActive. Passing false into this helps if you want to configure DSP based on unknown device factors, such as if you let the device choose the samplerate and channel count.
int azaMixerStreamOpen(azaMixer *data, azaMixerConfig config, azaStreamConfig streamConfig, bool activate);

// if preserveMixer is false, then we also call azaMixerDeinit.
void azaMixerStreamClose(azaMixer *data, bool preserveMixer);

static inline void azaMixerStreamSetActive(azaMixer *data, bool active) {
	azaStreamSetActive(&data->stream, active);
}

#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_MIXING_H