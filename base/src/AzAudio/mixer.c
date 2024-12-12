/*
	File: mixer.c
	Author: Philip Haynes
*/

#include "mixer.h"
#include "AzAudio.h"
#include "error.h"
#include "helpers.h"

#include <string.h>

int azaTrackInit(azaTrack *data, uint32_t bufferFrames, azaChannelLayout bufferChannelLayout) {
	return azaBufferInit(&data->buffer, bufferFrames, bufferChannelLayout);
}

void azaTrackDeinit(azaTrack *data) {
	azaBufferDeinit(&data->buffer);
	for (uint32_t i = 0; i < data->receives.count; i++) {
		azaChannelMatrixDeinit(&data->receives.data[i].channelMatrix);
	}
}

void azaTrackAppendDSP(azaTrack *data, azaDSP *dsp) {
	if (data->dsp) {
		azaDSP *nextDSP = data->dsp;
		while (nextDSP->pNext) {
			nextDSP = nextDSP->pNext;
		}
		nextDSP->pNext = dsp;
	} else {
		data->dsp = dsp;
	}
}

void azaTrackPrependDSP(azaTrack *data, azaDSP *dsp) {
	azaDSP *nextDSP = data->dsp;
	data->dsp = dsp;
	dsp->pNext = nextDSP;
}

int azaTrackConnect(azaTrack *from, azaTrack *to, float gain, azaTrackRoute **dstTrackRoute, uint32_t flags) {
	for (uint32_t i = 0; i < to->receives.count; i++) {
		if (to->receives.data[i].track == from) {
			to->receives.data[i].gain = gain;
			if (dstTrackRoute) {
				*dstTrackRoute = &to->receives.data[i];
			}
			return AZA_SUCCESS;
		}
	}
	azaTrackRoute route = {
		.track = from,
		.gain = gain,
	};
	int err = azaChannelMatrixInit(&route.channelMatrix, from->buffer.channelLayout.count, to->buffer.channelLayout.count);
	if (err) return err;
	if (!(flags & AZA_TRACK_CHANNEL_ROUTING_ZERO)) {
		azaChannelMatrixGenerateRoutingFromLayouts(&route.channelMatrix, from->buffer.channelLayout, to->buffer.channelLayout);
	}
	AZA_DYNAMIC_ARRAY_APPEND(azaTrackRoute, to->receives, route, return AZA_ERROR_OUT_OF_MEMORY);
	if (dstTrackRoute) {
		*dstTrackRoute = &to->receives.data[to->receives.count-1];
	}
	return AZA_SUCCESS;
}

void azaTrackDisconnect(azaTrack *from, azaTrack *to) {
	for (uint32_t i = 0; i < to->receives.count; i++) {
		if (to->receives.data[i].track == from) {
			azaChannelMatrixDeinit(&to->receives.data[i].channelMatrix);
			AZA_DYNAMIC_ARRAY_ERASE(to->receives, i, 1);
			break;
		}
	}
}

int azaTrackProcess(uint32_t frames, uint32_t samplerate, azaTrack *data) {
	data->buffer.samplerate = samplerate;
	azaBuffer buffer = azaBufferSlice(data->buffer, 0, frames);
	azaBufferZero(buffer);
	for (uint32_t i = 0; i < data->receives.count; i++) {
		azaTrackRoute *route = &data->receives.data[i];
		int err = azaTrackProcess(frames, samplerate, route->track);
		if (err) return err;
		// TODO: Latency compensation
		azaBufferMixMatrix(buffer, 1.0f, azaBufferSlice(route->track->buffer, 0, frames), aza_db_to_ampf(route->gain), &route->channelMatrix);
	}
	if (data->dsp) {
		return azaDSPProcessSingle(data->dsp, buffer);
	}
	return AZA_SUCCESS;
}

int azaMixerInit(azaMixer *data, azaMixerConfig config, azaChannelLayout bufferChannelLayout) {
	int err = AZA_SUCCESS;
	data->config = config;
	if (config.trackCount) {
		data->tracks = aza_calloc(config.trackCount, sizeof(azaTrack));
		if (!data->tracks) return AZA_ERROR_OUT_OF_MEMORY;
	}
	err = azaTrackInit(&data->master, config.bufferFrames, bufferChannelLayout);
	if (err) return err;
	for (uint32_t i = 0; i < config.trackCount; i++) {
		azaChannelLayout channelLayout;
		if (config.channelLayouts && config.channelLayouts[i].count) {
			channelLayout = config.channelLayouts[i];
		} else {
			channelLayout = bufferChannelLayout;
		}
		err = azaTrackInit(&data->tracks[i], config.bufferFrames, channelLayout);
		if (err) return err;
		err = azaTrackConnect(&data->tracks[i], &data->master, 0.0f, NULL, 0);
		if (err) return err;
	}
	return AZA_SUCCESS;
}

void azaMixerDeinit(azaMixer *data) {
	for (uint32_t i = 0; i < data->config.trackCount; i++) {
		azaTrackDeinit(&data->tracks[i]);
	}
	azaTrackDeinit(&data->master);
}

// Modified depth-first search for directed graphs to determine whether a cycle exists.
static int azaMixerCheckRoutingVisit(azaTrack *track) {
	for (uint32_t i = 0; i < track->receives.count; i++) {
		azaTrack *recv = track->receives.data[i].track;
		if (!recv) break;
		if (recv->mark == 2) continue;
		if (recv->mark == 1) return AZA_ERROR_MIXER_ROUTING_CYCLE;
		recv->mark = 1;
		if (azaMixerCheckRoutingVisit(recv)) return AZA_ERROR_MIXER_ROUTING_CYCLE;
		recv->mark = 2;
	}
	return AZA_SUCCESS;
}

static int azaMixerCheckRouting(azaMixer *data) {
	for (uint32_t i = 0; i < data->config.trackCount; i++) {
		data->tracks[i].mark = 0;
	}
	azaTrack *track = &data->master;
	track->mark = 0;
	return azaMixerCheckRoutingVisit(track);
}

int azaMixerProcess(uint32_t frames, uint32_t samplerate, azaMixer *data) {
	int err;
	if ((err = azaMixerCheckRouting(data))) return err;
	if ((err = azaTrackProcess(frames, samplerate, &data->master))) return err;
	return AZA_SUCCESS;
}

int azaMixerCallback(void *userdata, azaBuffer buffer) {
	azaMixer *mixer = (azaMixer*)userdata;
	azaBuffer stash = mixer->master.buffer;
	mixer->master.buffer = buffer;
	int err = azaMixerProcess(buffer.frames, buffer.samplerate, mixer);
	mixer->master.buffer = stash;
	return err;
}

int azaMixerStreamOpen(azaMixer *data, azaMixerConfig config, azaStreamConfig streamConfig, bool activate) {
	data->stream.mixCallback = azaMixerCallback;
	data->stream.userdata = data;
	int err;
	if ((err = azaStreamInit(&data->stream, streamConfig, AZA_OUTPUT, AZA_STREAM_COMMIT_FORMAT, false))) {
		char buffer[64];
		AZA_LOG_ERR(__FUNCTION__, " error: azaStreamInit failed (%s)\n", azaErrorString(err, buffer, sizeof(buffer)));
		return err;
	}
	config.bufferFrames = AZA_MAX(config.bufferFrames, azaStreamGetBufferFrameCount(&data->stream));
	azaMixerInit(data, config, azaStreamGetChannelLayout(&data->stream));
	if (activate) {
		azaStreamSetActive(&data->stream, true);
	}
	return AZA_SUCCESS;
}

void azaMixerStreamClose(azaMixer *data, bool preserveMixer) {
	azaStreamDeinit(&data->stream);
	if (!preserveMixer) {
		azaMixerDeinit(data);
	}
}