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
#include "plugins/azaDSPMultiplexer.h"

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
	if (!dsp->header.bypass && dsp->pFuncs->fp_getSpecs) {
		return dsp->pFuncs->fp_getSpecs(dsp, samplerate);
	}
	return (azaDSPSpecs) {0};
}

int azaDSPProcess(azaDSP *dsp, azaBuffer *dst, azaBuffer *src, uint32_t flags) {
	if (!dsp->header.bypass && dsp->pFuncs->fp_process && dsp->processMetadata.error == AZA_SUCCESS) {
		int result = dsp->pFuncs->fp_process(dsp, dst, src, flags);
		if (result != AZA_SUCCESS) {
			return result;
		}
		dsp->processMetadata.prevChannelCountDst = dst->channelLayout.count;
		dsp->processMetadata.prevChannelCountSrc = src->channelLayout.count;
	}
	return AZA_SUCCESS;
}

bool azaFreeDSP(azaDSP *dsp) {
	if (dsp->header.owned && dsp->pFuncs->fp_free) {
		dsp->pFuncs->fp_free(dsp);
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
	for (uint32_t i = 0; i < data->steps.count; i++) {
		azaDSP *dsp = data->steps.data[i].dsp;
		if (dsp) {
			azaFreeDSP(dsp);
		}
	}
	AZA_DA_DEINIT(data->steps);
	AZA_DA_DEINIT(data->buffer);
}

int azaDSPChainInitDuplicate(azaDSPChain *data, azaDSPChain *src) {
	int result = azaDSPChainInit(data, src->steps.count);
	if (result) {
		return result;
	}
	for (uint32_t i = 0; i < data->steps.count; i++) {
		azaDSP *dsp = src->steps.data[i].dsp;
		azaDSP *newDSP = dsp->pFuncs->fp_makeDuplicate(dsp);
		if (newDSP == NULL) {
			result = AZA_ERROR_OUT_OF_MEMORY;
			goto error;
		}
		data->steps.data[i].dsp = newDSP;
	}
	return result;
error:
	azaDSPChainDeinit(data);
	return result;
}

int azaDSPChainEnsureParity(azaDSPChain *data, azaDSPChain *src) {
	if (data->steps.count+1 == src->steps.count) {
		// Detect single plugin insertion
		int index = -1;
		for (int i = 0; i < (int)data->steps.count; i++) {
			if (data->steps.data[i].dsp->pFuncs != src->steps.data[i + (index >= 0 ? 1 : 0)].dsp->pFuncs) {
				if (index != -1) {
					// More than one mismatch, give up
					index = -2;
					break;
				}
				index = i;
			}
		}
		if (index == -1) {
			// It can only be at the end
			index = data->steps.count;
		}
		if (index >= 0) {
			// Detected one spot, insert it
			azaDSP *dsp = src->steps.data[index+1].dsp;
			azaDSP *newDSP = dsp->pFuncs->fp_makeDuplicate(dsp);
			if (newDSP == NULL) {
				return AZA_ERROR_OUT_OF_MEMORY;
			}
			int result = azaDSPChainInsertIndex(data, newDSP, index);
			if (result) {
				newDSP->pFuncs->fp_free(newDSP);
				return result;
			}
		}
	}
	if (data->steps.count == src->steps.count+1) {
		// Detect single plugin removal
		int index = -1;
		for (int i = 0; i < (int)data->steps.count; i++) {
			if (index < 0 && i >= (int)src->steps.count) {
				// Nothing found so we'd go out of bounds in the next check. Stop here.
				break;
			}
			if (data->steps.data[i].dsp->pFuncs != src->steps.data[i - (index >= 0 ? 1 : 0)].dsp->pFuncs) {
				if (index != -1) {
					// More than one mismatch, give up
					index = -2;
					break;
				}
				index = i;
			}
		}
		if (index == -1) {
			// It can only be the last one
			index = data->steps.count-1;
		}
		if (index >= 0) {
			// Detected one spot, remove it
			azaDSPChainRemoveIndex(data, index);
		}
	}
	bool hardReset = false;
	if (data->steps.count == src->steps.count) {
		// Check that all plugins are the same
		for (uint32_t i = 0; i < data->steps.count; i++) {
			if (data->steps.data[i].dsp->pFuncs != src->steps.data[i].dsp->pFuncs) {
				hardReset = true;
				break;
			}
		}
	} else {
		// Not even the same plugin count, and a simple pattern wasn't detected.
		hardReset = true;
	}
	if (hardReset) {
		azaDSPChainDeinit(data);
		return azaDSPChainInitDuplicate(data, src);
	}
	// Past this point all plugins are the same kinds, so just copy the configs over
	for (uint32_t i = 0; i < data->steps.count; i++) {
		azaDSP *dspDst = data->steps.data[i].dsp;
		azaDSP *dspSrc = src->steps.data[i].dsp;
		int result = dspSrc->pFuncs->fp_copyConfig(dspDst, dspSrc);
		if (result) {
			return result;
		}
	}
	return AZA_SUCCESS;
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
		if (step->dsp->processMetadata.error || step->dsp->header.bypass) {
			continue; // Don't change src to dst
		}
		if (src->leadingFrames < step->specs.leadingFrames) {
			AZA_LOG_ERR("Error(%s): For step %u (%s) src.leadingFrames (%u) < specs.leadingFrames(%u)\n", AZA_FUNCTION_NAME, i, step->dsp->guiMetadata.name, src->leadingFrames, step->specs.leadingFrames);
			step->dsp->processMetadata.error = AZA_ERROR_INVALID_FRAME_COUNT;
			goto next;
		}
		if (src->trailingFrames < step->specs.trailingFrames) {
			AZA_LOG_ERR("Error(%s): For step %u (%s) src.trailingFrames (%u) < specs.trailingFrames(%u)\n", AZA_FUNCTION_NAME, i, step->dsp->guiMetadata.name, src->trailingFrames, step->specs.trailingFrames);
			step->dsp->processMetadata.error = AZA_ERROR_INVALID_FRAME_COUNT;
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
			step->dsp->processMetadata.error = err;
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
	AZA_DA_RESERVE_COUNT(azaDSPRegistry, 12, return AZA_ERROR_OUT_OF_MEMORY);
	azaDSPAddRegEntry(azaCubicLimiterHeader);
	azaDSPAddRegEntry(azaLookaheadLimiterHeader);
	azaDSPAddRegEntry(azaFilterHeader);
	azaDSPAddRegEntry(azaLowPassFIRHeader);
	azaDSPAddRegEntry(azaCompressorHeader);
	azaDSPAddRegEntry(azaGateHeader);
	azaDSPAddRegEntry(azaDelayHeader);
	azaDSPAddRegEntry(azaDelayDynamicHeader);
	azaDSPAddRegEntry(azaReverbHeader);
	azaDSPAddRegEntry(azaSamplerHeader);
	azaDSPAddRegEntry(azaRMSHeader);
	azaDSPAddRegEntry(azaSpatializeHeader);
	azaDSPAddRegEntry(azaMonitorSpectrumHeader);
	azaDSPAddRegEntry(azaDSPDebuggerHeader);
	azaDSPAddRegEntry(azaDSPMultiplexerHeader);
	return AZA_SUCCESS;
}

void azaDSPRegistryDeinit() {
	AZA_DA_DEINIT(azaDSPRegistry);
}

int azaDSPAddRegEntry(azaDSP base) {
	AZA_DA_RESERVE_ONE_AT_END(azaDSPRegistry, return AZA_ERROR_OUT_OF_MEMORY);
	azaDSPRegEntry *dst = &azaDSPRegistry.data[azaDSPRegistry.count++];
	dst->base = base;
	return AZA_SUCCESS;
}



void azaOpAdd(float *lhs, float rhs) {
	*lhs += rhs;
}
void azaOpMax(float *lhs, float rhs) {
	*lhs = azaMaxf(*lhs, rhs);
}