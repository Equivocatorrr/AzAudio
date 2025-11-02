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
#include "AzAudio/dsp.h"
#include "AzAudio/backend/threads.h"

#include <stb_vorbis.c>



// Master

azaMixer mixer;

// Track 0

typedef struct Synth {
	azaDSP header;
	azaDSPChain outputEffects;
	azaFilter *filter;
	float gen[10];
	float lfo;
	int32_t impulseFrame;
} Synth;

int synthProcess(void *userdata, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	Synth *synth = userdata;
	float timestep = 1.0f / (float)dst->samplerate;
	for (uint32_t i = 0; i < dst->frames; i++) {
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
		for (uint8_t c = 0; c < dst->channelLayout.count; c++) {
			dst->pSamples[i * dst->stride + c] = sample;
		}
	}
	synth->impulseFrame -= dst->frames;
	if (synth->impulseFrame < 0) {
		synth->impulseFrame += 100000;
	}
	int err = azaDSPChainProcess(&synth->outputEffects, dst, dst, flags);
	if (err) return err;
	return AZA_SUCCESS;
}

void FreeSynth(void *dsp) {
	Synth *data = dsp;
	azaFreeFilter(data->filter);
	aza_free(data);
}

static const azaDSP SynthHeader = {
	/* .size         = */ sizeof(Synth),
	/* .version      = */ 1,
	/* .owned, bypass, selected, prevChannelCountDst, prevChannelCountSrc */ false, false, false, 0, 0,
	/* ._reserved    = */ {0},
	/* .error        = */ 0,
	/* .name         = */ "Synth",
	/* fp_getSpecs   = */ NULL,
	/* fp_process    = */ synthProcess,
	/* fp_free       = */ FreeSynth,
};

azaDSP* MakeDefaultSynth() {
	Synth *result = aza_calloc(1, sizeof(Synth));
	if (!result) return NULL;
	result->header = SynthHeader;
	if (azaDSPChainInit(&result->outputEffects, 1)) {
		goto fail;
	}
	result->filter = azaMakeFilter((azaFilterConfig) {
		.kind = AZA_FILTER_LOW_PASS,
		.frequency = 500.0f,
	});
	if (!result->filter) goto fail2;
	azaDSPChainAppend(&result->outputEffects, (azaDSP*)result->filter);
	memset(result->gen, 0, sizeof(result->gen));
	result->lfo = 0.25f;
	result->impulseFrame = 100000;
	return (azaDSP*)result;
fail2:
	azaDSPChainDeinit(&result->outputEffects);
fail:
	aza_free(result);
	return NULL;
}

// Track 1

azaBuffer bufferCat = {0};
azaSampler *samplerCat = NULL;
azaSpatialize *spatializeCat = NULL;

// Will adjust the rate that the spatialized sound sources move around
const float objectTimeScale = 1.0f;
// Will adjust how far the spatialized sound sources will travel
const float objectDistanceScale = 1.0f;

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
	timeDelta *= objectTimeScale;
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
				as * ec * distance * 10.0f * objectDistanceScale,
				es *      distance * 2.0f  * objectDistanceScale,
				ac * ec * distance * 5.0f  * objectDistanceScale,
			};
		}
		azaVec3 force = azaVec3NormalizedDef(azaSubVec3(object->target, object->pos), 0.001f, (azaVec3) { 0.0f, 1.0f, 0.0f });
		object->vel = azaAddVec3(object->vel, azaMulVec3Scalar(force, timeDelta * 1.0f));
		object->vel = azaMulVec3Scalar(object->vel, azaClampf(powf(2.0f, -timeDelta * 2.0f), 0.0f, 1.0f));
		object->posPrev = object->pos;
		object->pos = azaAddVec3(object->pos, azaMulVec3Scalar(object->vel, timeDelta));

	}
	// Set spatializer targets
	azaMutexLock(&mixer.mutex);
	for (uint8_t c = 0; c < count; c++) {
		spatializeCat->config.channels[c].target.amplitude = azaClampf(3.0f / azaVec3Norm(objects[c].pos), 0.0f, 1.0f);
		spatializeCat->config.channels[c].target.position = objects[c].pos;
	}
	azaMutexUnlock(&mixer.mutex);
}
/*
// OLD WAY
int catProcess(void *userdata, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	float timeDelta = (float)dst->frames / (float)dst->samplerate;
	int err = AZA_SUCCESS;
	updateObjects(bufferCat.channelLayout.count, timeDelta);
	azaBufferZero(dst);
	azaBuffer sampledBuffer = azaPushSideBufferZero(dst->frames, samplerCat->config.buffer->channelLayout.count, dst->samplerate);

#if 1
	if ((err = azaSamplerProcess(samplerCat, sampledBuffer))) {
		char buffer[64];
		AZA_LOG_ERR("azaSamplerProcess returned %s\n", azaErrorString(err, buffer, sizeof(buffer)));
		goto done;
	}
#else
	// Just a nyquist frequency to test aliasing
	for (uint32_t i = 0; i < sampledBuffer.frames; i+=2) {
		for (uint8_t c = 0; c < sampledBuffer.channelLayout.count; c++) {
			sampledBuffer.samples[(i+0)*sampledBuffer.stride + c] =  1.0f;
			sampledBuffer.samples[(i+1)*sampledBuffer.stride + c] = -1.0f;
		}
	}
#endif

#if 1
	for (uint8_t c = 0; c < bufferCat.channelLayout.count; c++) {
		float volumeStart = azaClampf(3.0f / azaVec3Norm(objects[c].posPrev), 0.0f, 1.0f);
		float volumeEnd = azaClampf(3.0f / azaVec3Norm(objects[c].pos), 0.0f, 1.0f);
		if ((err = azaSpatializeProcess(spatializeCat[c], buffer, azaBufferOneChannel(sampledBuffer, c), objects[c].posPrev, volumeStart, objects[c].pos, volumeEnd))) {
			char buffer[64];
			AZA_LOG_ERR("azaSpatializeProcess returned %s\n", azaErrorString(err, buffer, sizeof(buffer)));
			goto done;
		}
	}
#else
	// TODO: Don't do this, only works if file has same number of channels as the output device
	azaBufferCopy(buffer, sampledBuffer);
#endif
done:
	azaPopSideBuffer();
	return err;
}
*/



// Thread for handling console input (hacking it into a non-blocking input method, also mimicking how a game engine would behave)



azaMutex inputMutex;
azaThread inputThread;
bool shouldExit = false;

AZA_THREAD_PROC_DEF(inputThreadProc, userdata) {
	static const float semitone = 1.05946309436f;
	uint32_t lastId = 0;
	azaMixerGUIOpen(&mixer, /* onTop */ false);
	printf("Type Q to stop, M to open the mixer GUI, P to play, and S to stop, + to increase speed, - to decrease speed, = to reset speed, ? to print some stats\n");
	while (!shouldExit) {
		int c = getc(stdin);
		if (c == 'M' || c == 'm') {
			azaMixerGUIOpen(&mixer, /* onTop */ true);
		} else if (c == 'P' || c == 'p') {
			lastId = azaSamplerPlay(samplerCat, 1.0f, 0.0f);
		} else if (c == 'S' || c == 's') {
			azaSamplerStopAll(samplerCat);
		} else if (c == '+') {
			azaSamplerSetSpeed(samplerCat, lastId, azaSamplerGetSpeedTarget(samplerCat, lastId) * semitone);
		} else if (c == '-') {
			azaSamplerSetSpeed(samplerCat, lastId, azaSamplerGetSpeedTarget(samplerCat, lastId) / semitone);
		} else if (c == '=') {
			azaSamplerSetSpeed(samplerCat, lastId, 1.0f);
		} else if (c == '?') {
			printf("azaKernel stats:\n\tscalar samples: %llu\n\tvector samples: %llu\n", (unsigned long long)azaKernelScalarSamples, (unsigned long long)azaKernelVectorSamples);
		} else if (c == 'Q' || c == 'q') {
			azaMutexLock(&inputMutex);
			shouldExit = true;
			azaMutexUnlock(&inputMutex);
		}

		azaThreadSleep(10);
	}
	return 0;
}

bool ShouldExit() {
	bool result;
	azaMutexLock(&inputMutex);
	result = shouldExit;
	azaMutexUnlock(&inputMutex);
	return result;
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

#if 0
	// Just a nyquist frequency to test aliasing
	for (uint32_t i = 0; i < bufferCat.frames; i+=2) {
		for (uint8_t c = 0; c < bufferCat.channelLayout.count; c++) {
			bufferCat.samples[(i+0)*bufferCat.stride + c] =  1.0f;
			bufferCat.samples[(i+1)*bufferCat.stride + c] = -1.0f;
		}
	}
#endif

	// azaLogLevel = AZA_LOG_LEVEL_TRACE;
	int err = azaInit();
	if (err) {
		char buffer[64];
		fprintf(stderr, "Failed to azaInit (%s)\n", azaErrorString(err, buffer, sizeof(buffer)));
		return 1;
	}

	azaStreamConfig streamConfig = {
		0 // .samplerate = 44100
	};
	if ((err = azaMixerStreamOpen(&mixer, (azaMixerConfig) {0} , streamConfig, false))) {
		char buffer[64];
		fprintf(stderr, "Failed to azaMixerStreamOpen (%s)\n", azaErrorString(err, buffer, sizeof(buffer)));
		return 1;
	}



	// Configure the mixer and all the plugins



	azaDSPAddRegEntry(SynthHeader, MakeDefaultSynth);

	// Track 0

	azaTrack *track0;
	azaChannelLayout track0Layout = azaChannelLayoutMono();
	if ((err = azaMixerAddTrack(&mixer, -1, &track0, track0Layout, true))) {
		char buffer[64];
		fprintf(stderr, "Failed to azaMixerAddTrack (%s)\n", azaErrorString(err, buffer, sizeof(buffer)));
		return 1;
	}
	azaTrackSetName(track0, "Synth");

	azaDSP *dspSynth = MakeDefaultSynth();
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

	samplerCat = azaMakeSampler((azaSamplerConfig) {
		.buffer = &bufferCat,
		.speedTransitionTimeMs = 250.0f,
		.volumeTransitionTimeMs = 250.0f,
		.loop = true,
		.envelope = (azaADSRConfig) {
			.attack = 5.0f,
			.release = 500.0f,
		}
	});
	azaTrackAppendDSP(track1, (azaDSP*)samplerCat);

	objects = calloc(bufferCat.channelLayout.count, sizeof(Object));
	srand(123456); // We need repeatability for nullability-tests
	spatializeCat = (azaSpatialize*)azaMakeDefaultSpatialize();

	// spatializeCat->config.doDoppler = false;
	// spatializeCat->config.usePerChannelDelay = false;
	// spatializeCat->config.doFilter = false;
	spatializeCat->config.numSrcChannelsActive = bufferCat.channelLayout.count;

	updateObjects(bufferCat.channelLayout.count, 0.0f);

	azaTrackAppendDSP(track1, (azaDSP*)spatializeCat);

	for (uint8_t c = 0; c < bufferCat.channelLayout.count; c++) {
		objects[c].pos = objects[c].target;
	}

	// azaTrackAppendDSP(track1, (azaDSP*)&dspCat);

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
	});
	azaTrackAppendDSP(track2, (azaDSP*)reverbHighpass);

	azaReverb *reverb = azaMakeReverb((azaReverbConfig) {
		.gainWet = 0.0f,
		.muteDry = true,
		.roomsize = 5.0f,
		.color = 5.0f,
		.delay_ms = 0.0f,
	});
	azaTrackAppendDSP(track2, (azaDSP*)reverb);

	track2->mute = true;

	// Master

	azaCompressor *compressor = azaMakeCompressor((azaCompressorConfig) {
		.threshold = -24.0f,
		.ratio = 4.0f,
		.attack_ms = 10.0f,
		.decay_ms = 500.0f,
		.gainOutput = 6.0f,
	});
	compressor->header.bypass = true;
	azaTrackAppendDSP(&mixer.master, (azaDSP*)compressor);

	azaMonitorSpectrum *monitorSpectrum = (azaMonitorSpectrum*)azaMakeDefaultMonitorSpectrum();
	monitorSpectrum->config.ceiling = 0;
	// monitorSpectrum->header.bypass = true;
	azaTrackAppendDSP(&mixer.master, (azaDSP*)monitorSpectrum);

	azaLookaheadLimiter *limiter = azaMakeLookaheadLimiter((azaLookaheadLimiterConfig) {
		.gainInput  =  0.0f,
		.gainOutput = -0.1f,
	});
	// limiter->header.bypass = true;
	azaTrackAppendDSP(&mixer.master, (azaDSP*)limiter);

	mixer.master.gain = -12.0f;

	// Uncomment this to test if cyclic routing is detected
	// azaTrackConnect(&mixer.master, &mixer.tracks[0], 0.0f, NULL, 0);

	azaMixerStreamSetActive(&mixer, true);



	// Main loop!!!



	azaMutexInit(&inputMutex);
	azaThreadLaunch(&inputThread, inputThreadProc, NULL);

	const float timeDelta = 1.0f / 60.0f;

	// If the mixer processing reaches the target before we give it a new one, we can expect the pitch to abruptly jump to 1.0x instead of smoothly to whatever it needs to be due to the object movement.
	// As a hacky fix, make sure there's a little extra buffer time because our 60fps update interval doesn't match the mixer's update interval.
	// This hack will be replaced by scheduling and buffering.
	const float targetFollowTimeScale = 1.5f;

	spatializeCat->config.targetFollowTime_ms = targetFollowTimeScale * 1000.0f * timeDelta;

	while (!ShouldExit()) {
		updateObjects(bufferCat.channelLayout.count, timeDelta);
		azaThreadSleep((uint32_t)(1000.0f * timeDelta));
	}

	azaThreadJoin(&inputThread);
	azaMutexDeinit(&inputMutex);



	// Cleanup



	azaMixerGUIClose();
	azaMixerStreamClose(&mixer, false);

	FreeSynth((Synth*)dspSynth);

	free(objects);
	azaFreeSampler(samplerCat);
	azaFreeSpatialize(spatializeCat);
	azaBufferDeinit(&bufferCat, true);

	azaFreeFilter(reverbHighpass);
	azaFreeReverb(reverb);

	azaFreeCompressor(compressor);
	azaFreeMonitorSpectrum(monitorSpectrum);
	azaFreeLookaheadLimiter(limiter);

	azaDeinit();
	return 0;
}
