/*
	File: mixer.c
	Author: Philip Haynes
*/

#include "mixer.h"
#include "AzAudio.h"
#include "error.h"
#include "helpers.h"

#include "backend/timer.h"

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
	AZA_DA_APPEND(azaTrackRoute, to->receives, route, return AZA_ERROR_OUT_OF_MEMORY);
	if (dstTrackRoute) {
		*dstTrackRoute = &to->receives.data[to->receives.count-1];
	}
	return AZA_SUCCESS;
}

void azaTrackDisconnect(azaTrack *from, azaTrack *to) {
	for (uint32_t i = 0; i < to->receives.count; i++) {
		if (to->receives.data[i].track == from) {
			azaChannelMatrixDeinit(&to->receives.data[i].channelMatrix);
			AZA_DA_ERASE(to->receives, i, 1);
			break;
		}
	}
}

azaTrackRoute* azaTrackGetReceive(azaTrack *from, azaTrack *to) {
	for (uint32_t i = 0; i < to->receives.count; i++) {
		if (to->receives.data[i].track == from) return &to->receives.data[i];
	}
	return NULL;
}

int azaTrackProcess(uint32_t frames, uint32_t samplerate, azaTrack *data) {
	if (data->processed) return AZA_SUCCESS;
	data->buffer.samplerate = samplerate;
	azaBuffer buffer = azaBufferSlice(data->buffer, 0, frames);
	azaBufferZero(buffer);
	if (data->gain == -INFINITY || data->mute) {
		return AZA_SUCCESS;
	}
	int err = AZA_SUCCESS;
	for (uint32_t i = 0; i < data->receives.count; i++) {
		azaTrackRoute *route = &data->receives.data[i];
		if (route->mute) continue;
		err = azaTrackProcess(frames, samplerate, route->track);
		if (err) return err;
		// TODO: Latency compensation
		azaBufferMixMatrix(buffer, 1.0f, azaBufferSlice(route->track->buffer, 0, frames), aza_db_to_ampf(route->gain), &route->channelMatrix);
	}
	if (data->dsp) {
		err = azaDSPProcessSingle(data->dsp, buffer);
		if (err) return err;
	}
	if (data->gain != 0.0f) {
		float amp = aza_db_to_ampf(data->gain);
		for (uint32_t i = 0; i < buffer.frames; i++) {
			for (uint32_t c = 0; c < buffer.channelLayout.count; c++) {
				buffer.samples[i * buffer.stride + c] *= amp;
			}
		}
	}
	if (azaMixerGUIIsOpen()) {
		azaMetersUpdate(&data->meters, buffer, 1.0f);
	}
	data->processed = true;
	return AZA_SUCCESS;
}

int azaMixerInit(azaMixer *data, azaMixerConfig config, azaChannelLayout masterChannelLayout) {
	int err = AZA_SUCCESS;
	data->config = config;
	err = azaTrackInit(&data->master, config.bufferFrames, masterChannelLayout);
	if (err) return err;
	azaMutexInit(&data->mutex);
	data->tsOfflineStart = azaGetTimestamp();
	data->cpuPercent = 0.0f;
	return AZA_SUCCESS;
}

void azaMixerDeinit(azaMixer *data) {
	for (uint32_t i = 0; i < data->tracks.count; i++) {
		azaTrackDeinit(data->tracks.data[i]);
		aza_free(data->tracks.data[i]);
	}
	if (data->tracks.data) aza_free(data->tracks.data);
	data->tracks.count = 0;
	data->tracks.capacity = 0;
	azaTrackDeinit(&data->master);
	azaMutexDeinit(&data->mutex);
}

int azaMixerAddTrack(azaMixer *data, int32_t index, azaTrack **dst, azaChannelLayout channelLayout, bool connectToMaster) {
	int err = AZA_SUCCESS;
	azaTrack *result = (azaTrack*)aza_calloc(1, sizeof(azaTrack));
	if (!result) return AZA_ERROR_OUT_OF_MEMORY;
	if (index < 0) {
		AZA_DA_APPEND(azaTrack*, data->tracks, result, err = AZA_ERROR_OUT_OF_MEMORY);
	} else {
		AZA_DA_INSERT(azaTrack*, data->tracks, index, result, err = AZA_ERROR_OUT_OF_MEMORY);
	}
	if (err) goto fail;
	err = azaTrackInit(result, data->config.bufferFrames, channelLayout);
	if (err) goto fail;
	if (connectToMaster) {
		err = azaTrackConnect(result, &data->master, 0.0f, NULL, 0);
		if (err) goto fail2;
	}
	if (dst) {
		*dst = result;
	}
	return AZA_SUCCESS;
fail2:
	azaTrackDeinit(result);
fail:
	aza_free(result);
	return err;
}

// Modified depth-first search for directed graphs to determine whether a cycle exists.
static int azaMixerCheckRoutingVisit(azaTrack *track) {
	// Co-opt this search to reset whether track is processed
	track->processed = false;
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
	for (uint32_t i = 0; i < data->tracks.count; i++) {
		data->tracks.data[i]->mark = 0;
	}
	azaTrack *track = &data->master;
	track->mark = 0;
	return azaMixerCheckRoutingVisit(track);
}

int azaMixerProcess(uint32_t frames, uint32_t samplerate, azaMixer *data) {
	azaMutexLock(&data->mutex);
	int64_t tsStart = azaGetTimestamp();
	int64_t timeOffline = tsStart - data->tsOfflineStart;
	int err;
	if ((err = azaMixerCheckRouting(data))) goto error;
	if ((err = azaTrackProcess(frames, samplerate, &data->master))) goto error;
error:
	int64_t tsEnd = azaGetTimestamp();
	int64_t timeOnline = tsEnd - tsStart;
	float cpuPercent = (float)(100.0 * (double)timeOnline / (double)(timeOffline + timeOnline));
	data->cpuPercent = azaLerpf(data->cpuPercent, cpuPercent, 1.0f / (float)(1 + data->times % 20));
	data->times++;
	if ((data->times % 20) == 0) {
		data->cpuPercentSlow = data->cpuPercent;
	}
	data->tsOfflineStart = tsEnd;
	azaMutexUnlock(&data->mutex);
	return err;
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