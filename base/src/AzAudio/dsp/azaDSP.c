/*
	File: azaDSP.c
	Author: Philip Haynes
*/

#include "azaDSP.h"

#include "../AzAudio.h"
#include "../error.h"

#include "plugins/azaCubicLimiter.h"
#include "plugins/azaLookaheadLimiter.h"
#include "plugins/azaFilter.h"
#include "plugins/azaLowPassFIR.h"
#include "plugins/azaCompressor.h"
#include "plugins/azaGate.h"
#include "plugins/azaDelay.h"
#include "plugins/azaReverb.h"
#include "plugins/azaSampler.h"
#include "plugins/azaRMS.h"
#include "plugins/azaSpatialize.h"
#include "plugins/azaMonitorSpectrum.h"
#include "plugins/azaDSPDebugger.h"

void azaDSPSpecsCombineSerial(azaDSPSpecs *dst, azaDSPSpecs *src) {
	dst->latencyFrames += src->latencyFrames + src->trailingFrames;
	dst->leadingFrames = AZA_MAX(dst->leadingFrames, src->leadingFrames);
	dst->trailingFrames = AZA_MAX(dst->trailingFrames, src->trailingFrames);
}

void azaDSPSpecsCombineParallel(azaDSPSpecs *dst, azaDSPSpecs *src) {
	dst->latencyFrames = AZA_MAX(dst->latencyFrames, src->latencyFrames);
	dst->leadingFrames = AZA_MAX(dst->leadingFrames, src->leadingFrames);
	dst->trailingFrames = AZA_MAX(dst->trailingFrames, src->trailingFrames);
}

azaDSPSpecs azaDSPGetSpecs(azaDSP *dsp, uint32_t samplerate) {
	if (!dsp->bypass && dsp->fp_getSpecs) {
		return dsp->fp_getSpecs(dsp, samplerate);
	}
	return (azaDSPSpecs) {0};
}

int azaDSPProcess(azaDSP *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	if (!dsp->bypass && dsp->fp_process && dsp->error == AZA_SUCCESS) {
		int result = dsp->fp_process(dsp, dst, src, flags);
		if (result != AZA_SUCCESS) {
			return result;
		}
		dsp->prevChannelCountDst = dst->channelLayout.count;
		dsp->prevChannelCountSrc = src->channelLayout.count;
	}
	return AZA_SUCCESS;
}

bool azaFreeDSP(azaDSP *dsp) {
	if (dsp->owned && dsp->fp_free) {
		dsp->fp_free(dsp);
		return true;
	}
	return false;
}



int azaDSPChainInit(azaDSPChain *data, uint32_t stepsToReserve) {
	assert(data);
	memset(data, 0, sizeof(*data));
	if (stepsToReserve) {
		AZA_DA_RESERVE_COUNT(data->steps, stepsToReserve, return AZA_ERROR_OUT_OF_MEMORY);
	}
	return AZA_SUCCESS;
}

void azaDSPChainDeinit(azaDSPChain *data) {
	AZA_DA_DEINIT(data->steps);
	AZA_DA_DEINIT(data->buffer);
}

int azaDSPChainAppend(azaDSPChain *data, azaDSP *dsp) {
	AZA_DA_RESERVE_ONE_AT_END(data->steps, return AZA_ERROR_OUT_OF_MEMORY);
	data->steps.data[data->steps.count++] = (azaDSPChainStep) {
		.dsp = dsp,
		.bufferOffset = AZA_DSP_CHAIN_BUFFER_OFFSET_UNINITIALIZED,
	};
	return AZA_SUCCESS;
}

int azaDSPChainPrepend(azaDSPChain *data, azaDSP *dsp) {
	azaDSPChainStep step = {
		.dsp = dsp,
		.bufferOffset = AZA_DSP_CHAIN_BUFFER_OFFSET_UNINITIALIZED,
	};
	AZA_DA_INSERT(data->steps, 0, step, return AZA_ERROR_OUT_OF_MEMORY);
	return AZA_SUCCESS;
}

int azaDSPChainInsert(azaDSPChain *data, azaDSP *dsp, azaDSP *dst) {
	uint32_t index = UINT32_MAX;
	if (dst) {
		for (uint32_t i = 0; i < data->steps.count; i++) {
			if (data->steps.data[i].dsp == dst) {
				index = i;
				break;
			}
		}
		assert(index != UINT32_MAX && "dst is not found!!!");
	} else {
		index = data->steps.count;
	}
	azaDSPChainStep step = {
		.dsp = dsp,
		.bufferOffset = AZA_DSP_CHAIN_BUFFER_OFFSET_UNINITIALIZED,
	};
	AZA_DA_INSERT(data->steps, index, step, return AZA_ERROR_OUT_OF_MEMORY);
	return AZA_SUCCESS;
}

int azaDSPChainInsertIndex(azaDSPChain *data, azaDSP *dsp, uint32_t index) {
	azaDSPChainStep step = {
		.dsp = dsp,
		.bufferOffset = AZA_DSP_CHAIN_BUFFER_OFFSET_UNINITIALIZED,
	};
	AZA_DA_INSERT(data->steps, index, step, return AZA_ERROR_OUT_OF_MEMORY);
	return AZA_SUCCESS;
}

void azaDSPChainRemove(azaDSPChain *data, azaDSP *dsp) {
	uint32_t index = UINT32_MAX;
	for (uint32_t i = 0; i < data->steps.count; i++) {
		if (data->steps.data[i].dsp == dsp) {
			index = i;
			break;
		}
	}
	assert(index != UINT32_MAX && "dsp is not found!!!");
	AZA_DA_ERASE(data->steps, index, 1);
}

void azaDSPChainRemoveIndex(azaDSPChain *data, uint32_t index) {
	AZA_DA_ERASE(data->steps, index, 1);
}



azaDSPSpecs azaDSPChainGetSpecs(azaDSPChain *dsp, uint32_t samplerate) {
	azaDSPSpecs result = {0};
	for (uint32_t i = 0; i < dsp->steps.count; i++) {
		azaDSPSpecs specs = azaDSPGetSpecs(dsp->steps.data[i].dsp, samplerate);
		azaDSPSpecsCombineSerial(&result, &specs);
	}
	return result;
}



int azaDSPChainUpdate(azaDSPChain *data, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	if (data->steps.count == 0) {
		return AZA_SUCCESS;
	}
	assert(data->steps.count < 1024); // This is an insane number of steps. If we see this we know we have a bug.
	azaDSPSpecs *specs = alloca(sizeof(azaDSPSpecs) * data->steps.count);
	size_t neededSize = 0;
	azaBuffer *pSrc = src; // Need a copy because we loop again below
	for (uint32_t i = 0; i < data->steps.count; i++) {
		azaDSPChainStep *step = &data->steps.data[i];
		specs[i] = azaDSPGetSpecs(step->dsp, pSrc->samplerate);
		uint32_t extraFramesNeeded = specs[i].leadingFrames + specs[i].trailingFrames;
		neededSize += extraFramesNeeded * pSrc->channelLayout.count;
		pSrc = dst; // Because the first step will put the result into dst, so everything else must work with dst.
	}
	if (neededSize > data->buffer.capacity) {
		AZA_DA_RESERVE_COUNT(data->buffer, neededSize, return AZA_ERROR_OUT_OF_MEMORY);
	}
	data->buffer.count = 0;
	// Set buffer locations, zero it out the actual space if it moved or is uninitialized
	pSrc = src;
	for (uint32_t i = 0; i < data->steps.count; i++) {
		azaDSPChainStep *step = &data->steps.data[i];
		uint32_t numSamples = (step->specs.leadingFrames + step->specs.trailingFrames) * pSrc->channelLayout.count;
		if (step->bufferOffset != data->buffer.count || step->specs.leadingFrames != specs[i].leadingFrames || step->specs.trailingFrames != specs[i].trailingFrames) {
			// TODO: We almost definitely want to keep as much data as possible instead of just zeroing it out. I believe this is causing live changes in latency to become silent. Maybe we should disallow such continuous latency changes, but at the very least we can do better.
			// Should also capture when bufferOffset is AZA_DSP_CHAIN_BUFFER_OFFSET_UNINITIALIZED
			// Zero it out
			memset(data->buffer.data + data->buffer.count, 0, sizeof(float) * numSamples);
		}
		step->bufferOffset = data->buffer.count;
		step->specs = specs[i];
		data->buffer.count += numSamples;
		pSrc = dst; // Because the first step will put the result into dst, so everything else must work with dst.
	}
	return AZA_SUCCESS;
}

// |llll|mmmmmmmm|tttt|
// |bbbb|bbbbmmmm|mmmm|

int azaDSPChainProcessWithHandler(azaDSPChain *data, azaBuffer *dst, azaBuffer *src, uint32_t flags, fp_azaDSPChainProcess_OnPluginError fp_OnPluginError, void *userdata) {
	int err;
	err = azaDSPChainUpdate(data, dst, src, flags);
	if AZA_UNLIKELY(err) return err;
	for (uint32_t i = 0; i < data->steps.count; i++) {
		azaDSPChainStep *step = &data->steps.data[i];
		if (step->dsp->error || step->dsp->bypass) {
			continue; // Don't change src to dst
		}
		if (src->leadingFrames < step->specs.leadingFrames) {
			AZA_LOG_ERR("Error(%s): For step %u (%s) src.leadingFrames (%u) < specs.leadingFrames(%u)\n", AZA_FUNCTION_NAME, i, step->dsp->name, src->leadingFrames, step->specs.leadingFrames);
			step->dsp->error = AZA_ERROR_INVALID_FRAME_COUNT;
			goto next;
		}
		if (src->trailingFrames < step->specs.trailingFrames) {
			AZA_LOG_ERR("Error(%s): For step %u (%s) src.trailingFrames (%u) < specs.trailingFrames(%u)\n", AZA_FUNCTION_NAME, i, step->dsp->name, src->trailingFrames, step->specs.trailingFrames);
			step->dsp->error = AZA_ERROR_INVALID_FRAME_COUNT;
			goto next;
		}
		if (step->specs.trailingFrames) {
			// Move existing buffer forward by trailing frames
			size_t trailingSamples = src->channelLayout.count * step->specs.trailingFrames;
			size_t samples = src->channelLayout.count * src->frames;
			memmove(src->pSamples + trailingSamples, src->pSamples, sizeof(*src->pSamples) * samples);
		}
		size_t bufferFrames = step->specs.trailingFrames + step->specs.leadingFrames;
		if (bufferFrames) {
			// Copy last iteration's buffer to beginning of src, and get next iteration's buffer out of src
			size_t leadingSamples = src->channelLayout.count * step->specs.leadingFrames;
			size_t bufferSamples = src->channelLayout.count * bufferFrames;
			size_t srcSamples = src->channelLayout.count * src->frames;
			float *buffer = data->buffer.data + step->bufferOffset;
			memcpy(src->pSamples - leadingSamples, buffer, sizeof(*src->pSamples) * bufferSamples);
			memcpy(buffer, src->pSamples + srcSamples - leadingSamples, sizeof(*src->pSamples) * bufferSamples);
		}
		azaBuffer limitedSrc = azaBufferSliceEx(src, 0, src->frames, step->specs.leadingFrames, step->specs.trailingFrames);
		err = azaDSPProcess(step->dsp, dst, &limitedSrc, flags);
		if AZA_UNLIKELY(err) {
			step->dsp->error = err;
			if (fp_OnPluginError) {
				fp_OnPluginError(step->dsp, userdata);
			}
		}
	next:
		src = dst; // Only the first one can be transitive, because the first one puts the result in dst, so process only dst from here on out.
	}
	return AZA_SUCCESS;
}



azaDSPRegEntries azaDSPRegistry = {0};

int azaDSPRegistryInit() {
	AZA_DA_RESERVE_COUNT(azaDSPRegistry, 11, return AZA_ERROR_OUT_OF_MEMORY);
	azaDSPAddRegEntry(azaCubicLimiterHeader, azaMakeDefaultCubicLimiter);
	azaDSPAddRegEntry(azaLookaheadLimiterHeader, azaMakeDefaultLookaheadLimiter);
	azaDSPAddRegEntry(azaFilterHeader, azaMakeDefaultFilter);
	azaDSPAddRegEntry(azaLowPassFIRHeader, azaMakeDefaultLowPassFIR);
	azaDSPAddRegEntry(azaCompressorHeader, azaMakeDefaultCompressor);
	azaDSPAddRegEntry(azaGateHeader, azaMakeDefaultGate);
	azaDSPAddRegEntry(azaDelayHeader, azaMakeDefaultDelay);
	azaDSPAddRegEntry(azaDelayDynamicHeader, azaMakeDefaultDelayDynamic);
	azaDSPAddRegEntry(azaReverbHeader, azaMakeDefaultReverb);
	azaDSPAddRegEntry(azaSamplerHeader, azaMakeDefaultSampler);
	azaDSPAddRegEntry(azaRMSHeader, azaMakeDefaultRMS);
	azaDSPAddRegEntry(azaSpatializeHeader, azaMakeDefaultSpatialize);
	azaDSPAddRegEntry(azaMonitorSpectrumHeader, azaMakeDefaultMonitorSpectrum);
	azaDSPAddRegEntry(azaDSPDebuggerHeader, azaMakeDefaultDSPDebugger);
	return AZA_SUCCESS;
}

int azaDSPAddRegEntry(azaDSP base, azaDSP* (*fp_makeDSP)()) {
	AZA_DA_RESERVE_ONE_AT_END(azaDSPRegistry, return AZA_ERROR_OUT_OF_MEMORY);
	azaDSPRegEntry *dst = &azaDSPRegistry.data[azaDSPRegistry.count++];
	dst->base = base;
	dst->fp_makeDSP = fp_makeDSP;
	return AZA_SUCCESS;
}



void azaOpAdd(float *lhs, float rhs) {
	*lhs += rhs;
}
void azaOpMax(float *lhs, float rhs) {
	*lhs = azaMaxf(*lhs, rhs);
}