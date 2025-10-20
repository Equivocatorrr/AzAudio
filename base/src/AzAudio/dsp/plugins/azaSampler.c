/*
	File: azaSampler.c
	Author: Philip Haynes
*/

#include "azaSampler.h"

#include "../../AzAudio.h"
#include "../../math.h"
#include "../../error.h"
#include "../azaKernel.h"

// 13 plays nice with 8-wide SIMD
enum { AZA_SAMPLER_DESIRED_KERNEL_RADIUS = 13 };
static const float azaSamplerStopBand = 20000.0f;

void azaSamplerInit(azaSampler *data, azaSamplerConfig config) {
	data->header = azaSamplerHeader;
	data->config = config;
	data->numInstances = 0;
	azaMutexInit(&data->mutex);
}

void azaSamplerDeinit(azaSampler *data) {
	azaMutexDeinit(&data->mutex);
}

void azaSamplerReset(azaSampler *data) {
	azaMetersReset(&data->metersOutput);
}

void azaSamplerResetChannels(azaSampler *data, uint32_t firstChannel, uint32_t channelCount) {
	azaMetersResetChannels(&data->metersOutput, firstChannel, channelCount);
}

azaSampler* azaMakeSampler(azaSamplerConfig config) {
	azaSampler *result = aza_calloc(1, sizeof(azaSampler));
	if (result) {
		azaSamplerInit(result, config);
	}
	return result;
}

void azaFreeSampler(void *data) {
	azaSamplerDeinit(data);
	aza_free(data);
}

azaDSP* azaMakeDefaultSampler() {
	return (azaDSP*)azaMakeSampler((azaSamplerConfig) {
		.buffer = NULL,
		.speedTransitionTimeMs = 50.0f,
		.volumeTransitionTimeMs = 50.0f,
	});
}

int azaSamplerProcess(void *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	// Bypass and chaining handled by azaDSPProcess
	int err = AZA_SUCCESS;
	assert(dsp != NULL);
	azaSampler *data = (azaSampler*)dsp;

	if AZA_UNLIKELY(flags & AZA_DSP_PROCESS_FLAG_CUT) {
		azaSamplerReset(data);
	}

	err = azaCheckBuffer(dst);
	if AZA_UNLIKELY(err) return err;

	(void)src; // We don't care about src here

	if AZA_UNLIKELY(!data->config.buffer) {
		// Without a buffer we are nothing
		return AZA_SUCCESS;
	}
	/*
	if AZA_UNLIKELY(dst->channelLayout.count != data->config.buffer->channelLayout.count) {
		AZA_LOG_ERR("Error(%s): dst and config.buffer channel counts do not match! dst has %u channels and config.buffer has %u channels.\n", AZA_FUNCTION_NAME, (uint32_t)dst->channelLayout.count, (uint32_t)data->config.buffer->channelLayout.count);
		return AZA_ERROR_MISMATCHED_CHANNEL_COUNT;
	}
	*/

	// Instead of requiring the correct number of channels we'll just put in as many as we have for now. It's hacky, but we'll deal with that later.
	uint8_t channels = AZA_MIN(dst->channelLayout.count, data->config.buffer->channelLayout.count);

	azaMutexLock(&data->mutex);
	float samplerateFactor = (float)data->config.buffer->samplerate / (float)dst->samplerate;
	float deltaMs = 1000.0f / (float)data->config.buffer->samplerate;
	int32_t loopStart = data->config.loopStart >= (int32_t)data->config.buffer->frames ? 0 : data->config.loopStart;
	int32_t loopEnd = data->config.loopEnd <= loopStart ? data->config.buffer->frames : data->config.loopEnd;
	int32_t loopRegionLength = loopEnd - loopStart;
	// Keep our lowpass below the minimum nyquist frequency (leaving some extra space for the transition band to alias onto itself outside the range of human hearing)
	float stopBandFactor = azaClampf(2.0f * azaSamplerStopBand / (float)dst->samplerate, 0.25f, 1.0f);
	for (uint32_t inst = 0; inst < data->numInstances; inst++) {
		azaSamplerInstance *instance = &data->instances[inst];
		for (uint32_t i = 0; i < dst->frames; i++) {
			float volumeEnvelope = azaADSRUpdate(&data->config.envelope, &instance->envelope, deltaMs);
			if (instance->envelope.stage == AZA_ADSR_STAGE_STOP) {
				data->numInstances--;
				if ((int)inst < (int)data->numInstances) {
					memmove(data->instances+inst, data->instances+inst+1, (data->numInstances-inst) * sizeof(*data->instances));
				}
				inst -= 1;
				break;
			}
			float volumeGain = azaFollowerLinearUpdate(&instance->volume, deltaMs / data->config.volumeTransitionTimeMs);
			float volume = volumeEnvelope * volumeGain;
			float speed = azaFollowerLinearUpdate(&instance->speed, deltaMs / data->config.speedTransitionTimeMs);
			if AZA_UNLIKELY(volume == 0.0f) continue;
			speed *= samplerateFactor;
			if (speed == 1.0f && instance->fraction == 0.0f) {
				// No resampling necessary
				for (uint8_t c = 0; c < channels; c++) {
					float sample = data->config.buffer->pSamples[instance->frame * data->config.buffer->stride + c];
					dst->pSamples[i * dst->stride + c] += sample * volume;
				}
			} else {
				// Oof all resampling!
#if 0 // Old way!
				#define SAMPLER_KERNEL_RADIUS 15
				#define SAMPLER_KERNEL_SAMPLE_COUNT (SAMPLER_KERNEL_RADIUS*2+1)
				float kernelSamples[SAMPLER_KERNEL_SAMPLE_COUNT];
				float kernelIntegral = 0.0f;
				{ // Calculate interpolation kernel for our speed and offset
					// TODO: Definitely switch to using azaKernel, once it's been altered to allow low-passes below source nyquist, as is generally needed (this is a necessary step to fix aliasing issues in azaSpatialize and backend resampling as well).
					float rate = (speed > 1.0f ? 1.0f / speed : 1.0f) * stopBandFactor;
					float x = rate*(-instance->fraction - (float)SAMPLER_KERNEL_RADIUS);
					// Calculating it like this breaks down in the degenerate case where our kernel radius is too small to cover even the first lobe (because we're low-passing well below the source nyquist frequency). Doing this would create volume attenuation when rate is very low.
					// kernelIntegral = 1.0f / rate;
					for (int i = 0; i < SAMPLER_KERNEL_SAMPLE_COUNT; i++) {
						float amount = azaLanczosf(x, azaMaxf(floorf((float)(0+SAMPLER_KERNEL_RADIUS)*rate), 1.0f));
						kernelSamples[i] = amount;
						kernelIntegral += amount;
						x += rate;
					}
				}
				for (uint8_t c = 0; c < dst->channelLayout.count; c++) {
					float sample = 0.0f;
					for (int i = 0; i < SAMPLER_KERNEL_SAMPLE_COUNT; i++) {
						int frame = azaWrapi(i + (int)instance->frame - SAMPLER_KERNEL_RADIUS, data->config.buffer->frames);
						float amount = kernelSamples[i];
						sample += data->config.buffer->pSamples[frame * data->config.buffer->stride + c] * amount;
					}
					sample /= kernelIntegral;

					dst->pSamples[i * dst->stride + c] += sample * volume;
				}
#else // NEW WAY
				float rate = azaMinf(stopBandFactor / speed, 1.0f);
				// This value has to be <= AZA_KERNEL_DEFAULT_LANCZOS_COUNT
				azaKernel *kernel = azaKernelGetDefaultLanczos(azaKernelGetRadiusForRate(rate, AZA_SAMPLER_DESIRED_KERNEL_RADIUS));
				// TODO: Find some way to deal with the quiet pops you get from swapping out kernels
				float frame[AZA_MAX_CHANNEL_POSITIONS];
				azaSampleWithKernel(frame, channels, kernel, data->config.buffer->pSamples, data->config.buffer->stride, 0, data->config.buffer->frames, data->config.loop, instance->frame, instance->fraction, rate);
				for (uint8_t c = 0; c < channels; c++) {
					float sample = frame[c];
					dst->pSamples[i * dst->stride + c] += sample * volume;
				}
#endif
			}
			// TODO: Loop crossfades, because nobody likes a pop
			bool startedBeforeLoopEnd = instance->frame <= loopEnd;
			bool startedAfterLoopStart = instance->frame >= loopStart;
			if (instance->reverse) {
				instance->fraction -= speed;
			} else {
				instance->fraction += speed;
			}
			int32_t framesToAdd = (int32_t)truncf(instance->fraction);
			instance->frame += framesToAdd;
			instance->fraction -= framesToAdd;
			if (data->config.loop) {
				if (data->config.pingpong) {
					if (!instance->reverse && startedBeforeLoopEnd && instance->frame >= loopEnd) {
						// AZA_LOG_INFO("We're forwards going backwards (frame = %i, fraction= %f)!\n", instance->frame, instance->fraction);
						// - 1 because loopEnd is not considered a part of the range, and we definitely
						instance->frame = loopEnd+loopEnd - instance->frame - 1;
						instance->fraction = -instance->fraction;
						instance->reverse = true;
					} else if (instance->reverse && startedAfterLoopStart && instance->frame <= loopStart) {
						// AZA_LOG_INFO("We're backwards going forwards (frame = %i, fraction= %f)!\n", instance->frame, instance->fraction);
						// not - 1 because loopStart is considered a part of the range
						instance->frame = loopStart+loopStart - instance->frame;
						instance->fraction = -instance->fraction;
						instance->reverse = false;
					}
				} else {
					if (!instance->reverse && startedBeforeLoopEnd && instance->frame >= loopEnd) {
						instance->frame -= loopRegionLength;
					} else if (instance->reverse && startedAfterLoopStart && instance->frame <= loopStart) {
						instance->frame += loopRegionLength - 1;
					}
				}
			}
			if ((!instance->reverse && instance->frame >= (int32_t)data->config.buffer->frames) || (instance->reverse && instance->frame < 0)) {
				instance->envelope.stage = AZA_ADSR_STAGE_STOP;
			}
		}
	}

	if (data->header.selected) {
		azaMetersUpdate(&data->metersOutput, dst, 1.0f);
	}

	azaMutexUnlock(&data->mutex);
	return AZA_SUCCESS;
}

uint32_t azaSamplerPlay(azaSampler *data, float speed, float gainDB) {
	static uint32_t nextId = 1;
	azaMutexLock(&data->mutex);
	if (data->numInstances >= AZAUDIO_SAMPLER_MAX_INSTANCES) {
		azaMutexUnlock(&data->mutex);
		return 0;
	}
	uint32_t id = nextId++;
	// TODO: Do we have to care about id looparound? Maybe just use 64-bit ints to guarantee no matter what that we don't or handle overlap.
	uint32_t index = data->numInstances++;
	azaSamplerInstance *instance = &data->instances[index];
	instance->id = id;
	instance->frame = 0;
	instance->fraction = 0.0f;
	if (speed < 0.0f) {
		instance->frame = data->config.buffer->frames-1;
		instance->reverse = true;
		speed = -speed;
	}
	azaADSRStart(&instance->envelope);
	azaFollowerLinearJump(&instance->speed, speed);
	azaFollowerLinearJump(&instance->volume, aza_db_to_ampf(gainDB));
	azaMutexUnlock(&data->mutex);
	return id;
}

azaSamplerInstance* azaSamplerGetInstance(azaSampler *data, uint32_t id) {
	azaSamplerInstance *result = NULL;
	azaMutexLock(&data->mutex);
	for (uint32_t i = 0; i < data->numInstances; i++) {
		if (data->instances[i].id == id) {
			result = &data->instances[i];
		}
	}
	azaMutexUnlock(&data->mutex);
	return result;
}

static void azaSamplerStopInstance(azaSamplerInstance *data) {
	azaADSRStop(&data->envelope);
}

void azaSamplerStop(azaSampler *data, uint32_t id) {
	azaMutexLock(&data->mutex);
	azaSamplerInstance *instance = azaSamplerGetInstance(data, id);
	if (instance) {
		azaSamplerStopInstance(instance);
	}
	azaMutexUnlock(&data->mutex);
}

void azaSamplerStopAll(azaSampler *data) {
	azaMutexLock(&data->mutex);
	for (uint32_t i = 0; i < data->numInstances; i++) {
		azaSamplerStopInstance(&data->instances[i]);
	}
	azaMutexUnlock(&data->mutex);
}
