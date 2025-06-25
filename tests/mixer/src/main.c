/*
	File: main.c
	Author: singularity
	Program for testing sound spatialization.
*/

#include <stdlib.h>
#include <stdio.h>

#include "AzAudio/AzAudio.h"
#include "AzAudio/mixer.h"
#include "AzAudio/error.h"
#include "AzAudio/math.h"

#include <stb_vorbis.c>

// Master

azaMixer mixer;

// Track 0

typedef struct Synth {
	azaDSPUser header;
	azaFilter *filter;
	float gen[1];
	float lfo;
	int32_t impulseFrame;
} Synth;

int synthProcess(void *userdata, azaBuffer buffer) {
	Synth *synth = userdata;
	float timestep = 1.0f / (float)buffer.samplerate;
	for (uint32_t i = 0; i < buffer.frames; i++) {
		float sample = 0.0f;
		// float freqMul = (1.0f + (azaOscTriangle(synth->lfo) * 0.5f + 0.5f) * 9.0f);
		for (uint32_t o = 0; o < sizeof(synth->gen) / sizeof(float); o++) {
			float freq = (float)(o*2+1) * 100.0f;
			float amp = 1.0f / (float)(o*2+1);
			float genstep = timestep * freq;
			float antialiasingGain = azaLinstepf(genstep, 0.5f, 0.495f);
			if (antialiasingGain == 0.0f) break;
			sample += azaOscSine(synth->gen[o]) * amp * antialiasingGain;
			// sample += -azaOscSaw(synth->gen[o] + 0.5f) * amp * antialiasingGain;
			synth->gen[o] = azaWrap01f(synth->gen[o] + genstep);
		}
		// sample *= (1.0f + azaOscSine(synth->lfo)) * 0.5f;
		synth->lfo = azaWrap01f(synth->lfo + timestep * 0.1f);
		if ((int32_t)i == synth->impulseFrame) {
			sample = 10.0f;
		}
		for (uint8_t c = 0; c < buffer.channelLayout.count; c++) {
			buffer.samples[i * buffer.stride + c] = sample;
		}
	}
	synth->impulseFrame -= buffer.frames;
	if (synth->impulseFrame < 0) {
		synth->impulseFrame += 100000;
	}
	return AZA_SUCCESS;
}

azaDSP* MakeDefaultSynth(uint8_t channelCountInline) {
	Synth *result = aza_calloc(1, sizeof(Synth));
	if (!result) return NULL;
	azaDSPUserInitSingle(&result->header, sizeof(*result), "Synth", result, synthProcess);
	result->filter = azaMakeFilter((azaFilterConfig) {
		.kind = AZA_FILTER_LOW_PASS,
		.frequency = 500.0f,
	}, channelCountInline);
	result->header.header.pNext = (azaDSP*)result->filter;
	if (!result->filter) goto fail;
	result->gen[0] = 0.0f;
	result->lfo = 0.25f;
	result->impulseFrame = 100000;
	return (azaDSP*)result;
fail:
	aza_free(result);
	return NULL;
}

void FreeSynth(Synth *data) {
	azaFreeFilter(data->filter);
	aza_free(data);
}

// Track 1

azaBuffer bufferCat = {0};
azaSampler *samplerCat = NULL;
azaSpatialize **spatializeCat = NULL;
azaDSPUser dspCat;

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
	azaBufferInit(buffer, buffer->frames, buffer->channelLayout);
	stb_vorbis_get_samples_float_interleaved(vorbis, buffer->channelLayout.count, buffer->samples, buffer->frames * buffer->channelLayout.count);
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
	for (uint32_t i = 0; i < count; i++) {
		Object *object = &objects[i];
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
			float angleMin = angleSize * (float)i;
			float angleMax = angleSize * (float)(i+1);
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

int catProcess(void *userdata, azaBuffer buffer) {
	float timeDelta = (float)buffer.frames / (float)buffer.samplerate;
	int err;
	updateObjects(bufferCat.channelLayout.count, timeDelta);
	azaBufferZero(buffer);
	azaBuffer sampledBuffer = azaPushSideBufferZero(buffer.frames, samplerCat->config.buffer->channelLayout.count, buffer.samplerate);

	if ((err = azaSamplerProcess(samplerCat, sampledBuffer))) {
		char buffer[64];
		AZA_LOG_ERR("azaSamplerProcess returned %s\n", azaErrorString(err, buffer, sizeof(buffer)));
		goto done;
	}

	for (uint8_t c = 0; c < bufferCat.channelLayout.count; c++) {
		float volumeStart = azaClampf(3.0f / azaVec3Norm(objects[c].posPrev), 0.0f, 1.0f);
		float volumeEnd = azaClampf(3.0f / azaVec3Norm(objects[c].pos), 0.0f, 1.0f);
		if ((err = azaSpatializeProcess(spatializeCat[c], buffer, azaBufferOneChannel(sampledBuffer, c), objects[c].posPrev, volumeStart, objects[c].pos, volumeEnd))) {
			char buffer[64];
			AZA_LOG_ERR("azaSpatializeProcess returned %s\n", azaErrorString(err, buffer, sizeof(buffer)));
			goto done;
		}
	}
done:
	azaPopSideBuffer();
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
		char buffer[64];
		fprintf(stderr, "Failed to azaInit (%s)\n", azaErrorString(err, buffer, sizeof(buffer)));
		return 1;
	}

	if ((err = azaMixerStreamOpen(&mixer, (azaMixerConfig) {0} , (azaStreamConfig) {0}, false))) {
		char buffer[64];
		fprintf(stderr, "Failed to azaMixerStreamOpen (%s)\n", azaErrorString(err, buffer, sizeof(buffer)));
		return 1;
	}
	uint8_t outputChannelCount = azaStreamGetChannelLayout(&mixer.stream).count;

	// Configure all the DSP functions

	azaDSPAddRegEntry("Synth", MakeDefaultSynth, (void(*)(azaDSP*))FreeSynth);

	// Track 0

	azaTrack *track0;
	azaChannelLayout track0Layout = azaChannelLayoutMono();
	if ((err = azaMixerAddTrack(&mixer, -1, &track0, track0Layout, true))) {
		char buffer[64];
		fprintf(stderr, "Failed to azaMixerAddTrack (%s)\n", azaErrorString(err, buffer, sizeof(buffer)));
		return 1;
	}
	azaTrackSetName(track0, "Synth");

	azaDSP *dspSynth = MakeDefaultSynth(track0->buffer.channelLayout.count);
	azaTrackAppendDSP(track0, dspSynth);

	// We can use this to change the gain on an existing connection
	// azaTrackConnect(track0, &mixer.master, -6.0f, NULL, 0);
	track0->gain = 6.0f;
	track0->mute = true;

	// Track 1

	azaTrack *track1;
	// azaChannelLayout track1Layout = azaChannelLayoutMono();
	// azaChannelLayout track1Layout = azaChannelLayout_9_1();
	azaChannelLayout track1Layout = mixer.master.buffer.channelLayout;
	if ((err = azaMixerAddTrack(&mixer, -1, &track1, track1Layout, true))) {
		char buffer[64];
		fprintf(stderr, "Failed to azaMixerAddTrack (%s)\n", azaErrorString(err, buffer, sizeof(buffer)));
		return 1;
	}
	azaTrackSetName(track1, "Spatialized");

	azaDSPUserInitSingle(&dspCat, sizeof(dspCat), "Spatializer", NULL, catProcess);

	samplerCat = azaMakeSampler((azaSamplerConfig) {
		.buffer = &bufferCat,
		.speed = 1.0f,
		.gain = 0.0f,
	});

	objects = calloc(bufferCat.channelLayout.count, sizeof(Object));
	srand(123456); // We need repeatability for nullability-tests
	updateObjects(bufferCat.channelLayout.count, 0.0f);
	spatializeCat = malloc(sizeof(azaSpatialize*) * bufferCat.channelLayout.count);
	for (uint8_t c = 0; c < bufferCat.channelLayout.count; c++) {
		objects[c].pos = objects[c].target;
		spatializeCat[c] = azaMakeSpatialize((azaSpatializeConfig) {
			.world       = AZA_WORLD_DEFAULT,
			.mode        = AZA_SPATIALIZE_ADVANCED,
			.delayMax    = 0.0f,
			.earDistance = 0.0f,
		}, track1->buffer.channelLayout.count);
	}

	azaTrackAppendDSP(track1, (azaDSP*)&dspCat);

	// azaTrackConnect(track1, &mixer.master, -6.0f, NULL, 0);
	track1->gain = -6.0f;
	// track1->mute = true;

	// Track 2

	azaTrack *track2;
	// azaChannelLayout track2Layout = azaChannelLayout_9_0();
	azaChannelLayout track2Layout = mixer.master.buffer.channelLayout;
	if ((err = azaMixerAddTrack(&mixer, -1, &track2, track2Layout, true))) {
		char buffer[64];
		fprintf(stderr, "Failed to azaMixerAddTrack (%s)\n", azaErrorString(err, buffer, sizeof(buffer)));
		return 1;
	}
	azaTrackSetName(track2, "Reverb Bus");

	azaTrackConnect(track0, track2, -6.0f, NULL, 0);
	azaTrackConnect(track1, track2, -6.0f, NULL, 0);

	azaFilter *reverbHighpass = azaMakeFilter((azaFilterConfig) {
		.kind = AZA_FILTER_HIGH_PASS,
		.frequency = 50.0f,
		.dryMix = 0.0f,
	}, track2->buffer.channelLayout.count);
	azaTrackAppendDSP(track2, (azaDSP*)reverbHighpass);

	azaReverb *reverb = azaMakeReverb((azaReverbConfig) {
		.gain = 0.0f,
		.muteDry = true,
		.roomsize = 5.0f,
		.color = 5.0f,
		.delay = 0.0f,
	}, track2->buffer.channelLayout.count);
	azaTrackAppendDSP(track2, (azaDSP*)reverb);

	// track2->mute = true;

	// Master

	azaCompressor *compressor = azaMakeCompressor((azaCompressorConfig) {
		.threshold = -24.0f,
		.ratio = 4.0f,
		.attack = 10.0f,
		.decay = 500.0f,
		.gain = 12.0f,
	}, outputChannelCount);

	azaTrackAppendDSP(&mixer.master, (azaDSP*)compressor);

	azaLookaheadLimiter *limiter = azaMakeLookaheadLimiter((azaLookaheadLimiterConfig) {
		.gainInput  =  0.0f,
		.gainOutput = -0.1f,
	}, outputChannelCount);

	azaTrackAppendDSP(&mixer.master, (azaDSP*)limiter);

	mixer.master.gain = 0.0f;

	// Uncomment this to test if cyclic routing is detected
	// azaTrackConnect(&mixer.master, &mixer.tracks[0], 0.0f, NULL, 0);

	azaMixerStreamSetActive(&mixer, true);


	printf("Press ENTER to stop or type M first to open the mixer GUI\n");
	azaMixerGUIOpen(&mixer, /* onTop */ false);
	while (true) {
		int c = getc(stdin);
		if (c == 'M' || c == 'm') {
			azaMixerGUIOpen(&mixer, /* onTop */ true);
			do { c = getc(stdin); } while (c != EOF && c != '\n');
		} else {
			break;
		}
	}
	azaMixerGUIClose();
	azaMixerStreamClose(&mixer, false);

	free(objects);
	azaFreeSampler(samplerCat);
	for (uint8_t c = 0; c < bufferCat.channelLayout.count; c++) {
		azaFreeSpatialize(spatializeCat[c]);
	}
	free(spatializeCat);
	azaBufferDeinit(&bufferCat);

	azaDeinit();
	return 0;
}
