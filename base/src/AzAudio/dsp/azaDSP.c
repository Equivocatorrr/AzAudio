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

void azaDSPSpecsCombineSerial(azaDSPSpecs *dst, azaDSPSpecs *src) {
	dst->latencyFrames += src->latencyFrames;
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

azaDSPSpecs azaDSPChainGetSpecsBackward(azaDSP *dsp, uint32_t samplerate) {
	azaDSPSpecs result = {0};
	while (dsp) {
		azaDSPSpecs specs = azaDSPGetSpecs(dsp, samplerate);
		azaDSPSpecsCombineSerial(&result, &specs);
		dsp = dsp->pPrev;
	}
	return result;
}

azaDSPSpecs azaDSPChainGetSpecsForward(azaDSP *dsp, uint32_t samplerate) {
	azaDSPSpecs result = {0};
	while (dsp) {
		azaDSPSpecs specs = azaDSPGetSpecs(dsp, samplerate);
		azaDSPSpecsCombineSerial(&result, &specs);
		dsp = dsp->pNext;
	}
	return result;
}

azaDSPSpecs azaDSPChainGetSpecs(azaDSP *dsp, uint32_t samplerate) {
	azaDSPSpecs result = {0};
	while (dsp->pPrev) {
		dsp = dsp->pPrev;
	}
	while (dsp) {
		azaDSPSpecs specs = azaDSPGetSpecs(dsp, samplerate);
		azaDSPSpecsCombineSerial(&result, &specs);
		dsp = dsp->pNext;
	}
	return result;
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