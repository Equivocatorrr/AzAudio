/*
	File: dsp.h
	Author: Philip Haynes
	Includes all the plugins and dsp utilities
*/

#ifndef AZAUDIO_DSP_H
#define AZAUDIO_DSP_H

#include "utility.h"
#include "azaChannelMatrix.h"
#include "azaBuffer.h"
#include "azaMeters.h"
#include "azaDSP.h"
#include "azaKernel.h"

// plugins

#include "plugins/azaRMS.h"
#include "plugins/azaCubicLimiter.h"
#include "plugins/azaLookaheadLimiter.h"
#include "plugins/azaFilter.h"
#include "plugins/azaLowPassFIR.h"
#include "plugins/azaCompressor.h"
#include "plugins/azaGate.h"
#include "plugins/azaDelay.h"
#include "plugins/azaDelayDynamic.h"
#include "plugins/azaReverb.h"
#include "plugins/azaSampler.h"
#include "plugins/azaSpatialize.h"
#include "plugins/azaMonitorSpectrum.h"

#endif // AZAUDIO_DSP_H