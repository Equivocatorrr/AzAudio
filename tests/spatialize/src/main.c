/*
	File: main.c
	Author: singularity
	Program for testing sound spatialization.
*/

#include <stdlib.h>
#include <stdio.h>

#include "AzAudio/AzAudio.h"
#include "AzAudio/dsp.h"
#include "AzAudio/error.h"
#include "AzAudio/math.h"

#include <stb_vorbis.c>

azaBuffer bufferCat = {0};
azaSampler *sampler = NULL;
azaSpatialize *spatialize = NULL;
azaReverb *reverb = NULL;
azaFilter *reverbFilter = NULL;
azaLookaheadLimiter *limiter = NULL;

#define PRINT_OBJECT_INFO 0

typedef struct Object {
	azaVec3 posPrev;
	azaVec3 pos;
	azaVec3 vel;
	azaVec3 target;
#if PRINT_OBJECT_INFO
	float timer;
#endif
} Object;

Object *objects;

int loadSoundFileIntoBuffer(azaBuffer *buffer, const char *filename) {
	int err;
	stb_vorbis *vorbis = stb_vorbis_open_filename(filename, &err, NULL);
	if (!vorbis) {
		fprintf(stderr, "Failed to load sound \"%s\": (%d)\n", filename, err);
		return 1;
	}
	buffer->frames = stb_vorbis_stream_length_in_samples(vorbis);
	stb_vorbis_info info = stb_vorbis_get_info(vorbis);
	printf("Sound \"%s\" has %u channels and a samplerate of %u\n", filename, info.channels, info.sample_rate);
	buffer->channelLayout.count = info.channels;
	buffer->samplerate = info.sample_rate;
	azaBufferInit(buffer, buffer->frames, 0, 0, buffer->channelLayout);
	stb_vorbis_get_samples_float_interleaved(vorbis, buffer->channelLayout.count, buffer->pSamples, buffer->frames * buffer->channelLayout.count);
	stb_vorbis_close(vorbis);
	return 0;
}

float randomf(float min, float max) {
	float val = (float)((uint32_t)rand());
	val /= (float)RAND_MAX;
	val = val * (max - min) + min;
	return val;
}

void updateObjects(uint32_t count, float timeDelta) {
	if (count == 0) return;
	float angleSize = AZA_TAU / (float)count;
	for (uint8_t c = 0; c < count; c++) {
		Object *object = &objects[c];
#if PRINT_OBJECT_INFO
		if (object->timer <= 0.0f) {
			printf("target = { %f, %f, %f }\n", object->target.x, object->target.y, object->target.z);
			printf("pos = { %f, %f, %f }\n", object->pos.x, object->pos.y, object->pos.z);
			printf("vel = { %f, %f, %f }\n", object->vel.x, object->vel.y, object->vel.z);
			object->timer += 0.5f;
		}
		object->timer -= timeDelta;
#endif
		if (azaVec3NormSqr(azaSubVec3(object->pos, object->target)) < azaSqrf(0.1f)) {
			float angleMin = angleSize * (float)c;
			float angleMax = angleSize * (float)(c+1);
			float azimuth = randomf(angleMin, angleMax);
			float ac = cosf(azimuth), as = sinf(azimuth);
			float elevation = randomf(-AZA_TAU/4.0f, AZA_TAU/4.0f);
			float ec = cosf(elevation), es = sinf(elevation);
			float distance = sqrtf(randomf(0.0f, 1.0f));
			object->target = (azaVec3) {
				as * ec * distance * 10.0f,
				es * distance * 2.0f,
				ac * ec * distance * 5.0f,
			};
		}
		azaVec3 force = azaVec3NormalizedDef(azaSubVec3(object->target, object->pos), 0.001f, (azaVec3) { 0.0f, 1.0f, 0.0f });
		object->vel = azaAddVec3(object->vel, azaMulVec3Scalar(force, timeDelta * 1.0f));
		object->vel = azaMulVec3Scalar(object->vel, azaClampf(powf(2.0f, -timeDelta * 2.0f), 0.0f, 1.0f));
		object->posPrev = object->pos;
		object->pos = azaAddVec3(object->pos, azaMulVec3Scalar(object->vel, timeDelta));
	}
}

int processCallbackOutput(void *userdata, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	float timeDelta = (float)dst->frames / (float)dst->samplerate;
	int err = AZA_SUCCESS;
	char errorString[64];
	updateObjects(bufferCat.channelLayout.count, timeDelta);
	azaBufferZero(dst);

	if ((err = azaSamplerProcess(sampler, dst, src, flags))) {
		AZA_LOG_ERR("azaSamplerProcess returned %s\n", azaErrorString(err, errorString, sizeof(errorString)));
		goto done;
	}

	azaSpatializeChannelConfig start[AZA_MAX_CHANNEL_POSITIONS];
	azaSpatializeChannelConfig end[AZA_MAX_CHANNEL_POSITIONS];
	for (uint8_t c = 0; c < bufferCat.channelLayout.count; c++) {
		start[c] = (azaSpatializeChannelConfig) {{
			.amplitude = azaClampf(3.0f / azaVec3Norm(objects[c].posPrev), 0.0f, 1.0f),
			.position = objects[c].posPrev,
		}};
		end[c] = (azaSpatializeChannelConfig) {{
			.amplitude = azaClampf(3.0f / azaVec3Norm(objects[c].pos), 0.0f, 1.0f),
			.position = objects[c].pos,
		}};
	}
	azaSpatializeSetRamps(spatialize, bufferCat.channelLayout.count, start, end, dst->frames, dst->samplerate);
	if ((err = azaSpatializeProcess(spatialize, dst, dst, flags))) {
		AZA_LOG_ERR("azaSpatializeProcess returned %s\n", azaErrorString(err, errorString, sizeof(errorString)));
		goto done;
	}
	if ((err = azaReverbProcess(reverb, dst, dst, flags))) {
		AZA_LOG_ERR("azaReverbProcess returned %s\n", azaErrorString(err, errorString, sizeof(errorString)));
		goto done;
	}
	if ((err = azaLookaheadLimiterProcess(limiter, dst, dst, flags))) {
		AZA_LOG_ERR("azaLookaheadLimiterProcess returned %s\n", azaErrorString(err, errorString, sizeof(errorString)));
		goto done;
	}
done:
	return err;
}

void usage(const char *executableName) {
	printf(
		"Usage:\n"
		"%s                      Listen to a purring cat move around\n"
		"%s --help               Display this help\n"
		"%s path/to/sound.ogg    Play the given sound file\n"
	, executableName, executableName, executableName);
}

int main(int argumentCount, char** argumentValues) {
	const char *soundFilename = "data/cat purring loop.ogg";

	if (argumentCount >= 2) {
		if (strcmp(argumentValues[1], "--help") == 0) {
			usage(argumentValues[0]);
			return 0;
		} else {
			soundFilename = argumentValues[1];
		}
	}

	if (loadSoundFileIntoBuffer(&bufferCat, soundFilename)) return 1;
	if (bufferCat.channelLayout.count == 0) {
		fprintf(stderr, "Sound \"%s\" has no channels!\n", soundFilename);
		return 1;
	}

	int err = azaInit();
	if (err) {
		fprintf(stderr, "Failed to azaInit!\n");
		return 1;
	}

	azaStream streamOutput = {0};
	streamOutput.processCallback = processCallbackOutput;
	if ((err = azaStreamInitDefault(&streamOutput, AZA_OUTPUT, false)) != AZA_SUCCESS) {
		char buffer[64];
		fprintf(stderr, "Failed to init output stream! (%s)\n", azaErrorString(err, buffer, sizeof(buffer)));
		return 1;
	}



	// Configure all the DSP functions



	sampler = azaMakeSampler((azaSamplerConfig) {
		.buffer = &bufferCat,
		.speedTransitionTimeMs = 50.0f,
		.volumeTransitionTimeMs = 50.0f,
		.loop = true,
	});
	azaSamplerPlay(sampler, 1.0f, 0.0f);

	objects = calloc(bufferCat.channelLayout.count, sizeof(Object));
	updateObjects(bufferCat.channelLayout.count, 0.0f);
	for (uint8_t c = 0; c < bufferCat.channelLayout.count; c++) {
		objects[c].pos = objects[c].target;
	}
	spatialize = (azaSpatialize*)azaMakeDefaultSpatialize();

	reverb = azaMakeReverb((azaReverbConfig) {
		.gainWet = -6.0f,
		.gainDry = 0.0f,
		.roomsize = 40.0f,
		.color = 3.0f,
		.delay_ms = 23.0f,
	});

	reverbFilter = azaMakeFilter((azaFilterConfig) {
		.kind = AZA_FILTER_HIGH_PASS,
		.poles = AZA_FILTER_6_DB,
		.frequency = 200.0f,
	});
	azaDSPChainAppend(&reverb->inputDelay.inputEffects, (azaDSP*)reverbFilter);

	limiter = azaMakeLookaheadLimiter((azaLookaheadLimiterConfig) {
		.gainInput  =  3.0f,
		.gainOutput = -0.1f,
	});

	azaStreamSetActive(&streamOutput, 1);
	printf("Press ENTER to stop\n");
	getc(stdin);
	azaStreamDeinit(&streamOutput);

	free(objects);
	azaFreeSampler(sampler);
	azaFreeSpatialize(spatialize);
	azaFreeLookaheadLimiter(limiter);
	azaBufferDeinit(&bufferCat, true);

	azaDeinit();
	return 0;
}
