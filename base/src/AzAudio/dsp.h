/*
	File: dsp.h
	Author: Philip Haynes
	Includes all the plugins and dsp utilities
*/

#ifndef AZAUDIO_DSP_H
#define AZAUDIO_DSP_H

#include "dsp/utility.h"
#include "dsp/azaChannelMatrix.h"
#include "dsp/azaBuffer.h"
#include "dsp/azaMeters.h"
#include "dsp/azaDSP.h"
#include "dsp/azaKernel.h"

// plugins

#include "dsp/plugins/azaRMS.h"
#include "dsp/plugins/azaCubicLimiter.h"
#include "dsp/plugins/azaLookaheadLimiter.h"
#include "dsp/plugins/azaFilter.h"
#include "dsp/plugins/azaCompressor.h"
#include "dsp/plugins/azaGate.h"
#include "dsp/plugins/azaDelay.h"
#include "dsp/plugins/azaDelayDynamic.h"
#include "dsp/plugins/azaReverb.h"
#include "dsp/plugins/azaSampler.h"
#include "dsp/plugins/azaSpatialize.h"
#include "dsp/plugins/azaMonitorSpectrum.h"

#endif // AZAUDIO_DSP_H