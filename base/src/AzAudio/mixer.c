/*
	File: mixer.c
	Author: Philip Haynes
*/

#include "mixer.h"
#include "AzAudio.h"
#include "math.h"
#include "error.h"

#include "backend/timer.h"

#include <string.h>

int azaTrackInit(azaTrack *data, uint32_t bufferFrames, azaChannelLayout bufferChannelLayout) {
	return azaBufferInit(&data->buffer, bufferFrames, 0, 0, bufferChannelLayout);
}

void azaTrackDeinit(azaTrack *data) {
	azaBufferDeinit(&data->buffer, true);
	azaDSP *dsp = data->dsp;
	while (dsp) {
		azaDSP *nextDSP = dsp->pNext;
		if (dsp->owned) {
			if (!azaFreeDSP(dsp)) {
				// TODO: Move this error into the GUI as well
				AZA_LOG_ERR("Failed to free \"%s\" because a free function is not given.\n", dsp->name);
				dsp->pNext = NULL;
			}
		}
		dsp = nextDSP;
	}
	data->dsp = NULL;
	for (uint32_t i = 0; i < data->receives.count; i++) {
		azaTrackRouteDeinit(&data->receives.data[i]);
	}
	AZA_DA_DEINIT(data->receives);
}

void azaTrackAppendDSP(azaTrack *data, azaDSP *dsp) {
	azaDSP **ppdst = &data->dsp;
	while (*ppdst) {
		ppdst = &(*ppdst)->pNext;
	}
	*ppdst = dsp;
}

void azaTrackPrependDSP(azaTrack *data, azaDSP *dsp) {
	azaDSP *nextDSP = data->dsp;
	data->dsp = dsp;
	dsp->pNext = nextDSP;
}

void azaTrackInsertDSP(azaTrack *data, azaDSP *dsp, azaDSP *dst) {
	azaDSP **ppdst = &data->dsp;
	while (true) {
		if (*ppdst == dst) {
			dsp->pNext = *ppdst;
			*ppdst = dsp;
			break;
		}
		assert(*ppdst); // Means dst is not found.
		ppdst = &(*ppdst)->pNext;
	}
}

void azaTrackRemoveDSP(azaTrack *data, azaDSP *dsp) {
	azaDSP **ppdst = &data->dsp;
	while (*ppdst) {
		if (*ppdst == dsp) {
			*ppdst = dsp->pNext;
			break;
		}
		ppdst = &(*ppdst)->pNext;
	}
}

void azaTrackSetName(azaTrack *data, const char *name) {
	if (!name) {
		data->name[0] = 0;
	} else {
		aza_strcpy(data->name, name, sizeof(data->name));
	}
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
	AZA_DA_APPEND(to->receives, route, return AZA_ERROR_OUT_OF_MEMORY);
	if (dstTrackRoute) {
		*dstTrackRoute = &to->receives.data[to->receives.count-1];
	}
	return AZA_SUCCESS;
}

void azaTrackDisconnect(azaTrack *from, azaTrack *to) {
	for (uint32_t i = 0; i < to->receives.count; i++) {
		if (to->receives.data[i].track == from) {
			azaTrackRouteDeinit(&to->receives.data[i]);
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
	azaBuffer buffer = azaBufferSlice(&data->buffer, 0, frames);
	azaBufferZero(&buffer);
	if (data->gain == -INFINITY || data->mute) {
		return AZA_SUCCESS;
	}
	int err = AZA_SUCCESS;
	for (uint32_t i = 0; i < data->receives.count; i++) {
		azaTrackRoute *route = &data->receives.data[i];
		if (route->mute || route->track->mute) continue;
		err = azaTrackProcess(frames, samplerate, route->track);
		if (err) return err;
		// TODO: Latency compensation
		azaBuffer srcBuffer = azaBufferSlice(&route->track->buffer, 0, frames);
		azaBufferMixMatrix(&buffer, 1.0f, &srcBuffer, aza_db_to_ampf(route->gain), &route->channelMatrix);
	}
	if (data->dsp) {
		azaDSP *dspStart = data->dsp;
		azaDSP *dsp = dspStart;
		while (dsp) {
			// TODO: Check when track configuration changed so we can pass the appropriate flag
			err = azaDSPProcess(dsp, &buffer, &buffer, 0);
			if (err) {
				dsp->error = err;
				if (azaMixerGUIIsOpen()) {
					azaMixerGUIShowError(azaGetLastErrorMessage());
				}
			}
			dsp = dsp->pNext;
			if (dsp == dspStart) {
				return AZA_ERROR_MIXER_ROUTING_CYCLE;
			}
		}
	}
	if (data->gain != 0.0f) {
		float amp = aza_db_to_ampf(data->gain);
		for (uint32_t i = 0; i < buffer.frames; i++) {
			for (uint32_t c = 0; c < buffer.channelLayout.count; c++) {
				buffer.pSamples[i * buffer.stride + c] *= amp;
			}
		}
	}
	if (azaMixerGUIIsOpen()) {
		azaMetersUpdate(&data->meters, &buffer, 1.0f);
	}
	data->processed = true;
	return AZA_SUCCESS;
}

int azaMixerInit(azaMixer *data, azaMixerConfig config, azaChannelLayout masterChannelLayout) {
	int err = AZA_SUCCESS;
	data->config = config;
	err = azaTrackInit(&data->master, config.bufferFrames, masterChannelLayout);
	if (err) return err;
	azaTrackSetName(&data->master, "Master");
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
	azaMutexLock(&data->mutex);
	if (index < 0) {
		AZA_DA_APPEND(data->tracks, result, err = AZA_ERROR_OUT_OF_MEMORY);
	} else {
		AZA_DA_INSERT(data->tracks, index, result, err = AZA_ERROR_OUT_OF_MEMORY);
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
	azaMutexUnlock(&data->mutex);
	return AZA_SUCCESS;
fail2:
	azaTrackDeinit(result);
fail:
	aza_free(result);
	azaMutexUnlock(&data->mutex);
	return err;
}

void azaMixerRemoveTrack(azaMixer *data, int32_t index) {
	assert(index >= 0);
	assert(index < (int32_t)data->tracks.count);
	azaMutexLock(&data->mutex);
	azaTrack *track = data->tracks.data[index];
	azaTrackDisconnect(track, &data->master);
	for (uint32_t i = 0; i < data->tracks.count; i++) {
		// Remove our receives from all the tracks
		azaTrack *other = data->tracks.data[i];
		azaTrackDisconnect(track, other);
	}
	azaTrackDeinit(data->tracks.data[index]);
	AZA_DA_ERASE(data->tracks, index, 1);
	azaMutexUnlock(&data->mutex);
}

int azaMixerGetTrackSendCount(azaMixer *mixer, azaTrack *track) {
	int count = (azaTrackGetReceive(track, &mixer->master) != NULL);
	for (uint32_t i = 0; i < mixer->tracks.count; i++) {
		if (azaTrackGetReceive(track, mixer->tracks.data[i])) count++;
	}
	return count;
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

int azaMixerCallback(void *userdata, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	azaMixer *mixer = (azaMixer*)userdata;
	azaBuffer stash = mixer->master.buffer;
	mixer->master.buffer = azaBufferView(dst);
	if (dst != src) {
		azaBufferCopy(dst, src);
	}
	int err = azaMixerProcess(dst->frames, dst->samplerate, mixer);
	if (err == AZA_ERROR_MIXER_ROUTING_CYCLE) {
		// Gracefully zero out audio since a cycle can be remedied with the mixer GUI now
		mixer->hasCircularRouting = true;
		azaBufferZero(dst);
		err = AZA_SUCCESS;
	} else {
		mixer->hasCircularRouting = false;
	}
	mixer->master.buffer = stash;
	return err;
}

int azaMixerStreamOpen(azaMixer *data, azaMixerConfig config, azaStreamConfig streamConfig, bool activate) {
	data->stream.processCallback = azaMixerCallback;
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