/*
	File: dsp.c
	Author: Philip Haynes
	Unity build of things in the dsp folder, currently unused because CMake is weird about this
*/

#include "dsp/utility.c"
#include "dsp/azaMeters.c"
#include "dsp/azaDSP.c"
#include "dsp/azaBuffer.c"
#include "dsp/azaKernel.c"
#include "dsp/azaChannelMatrix.c"

// plugins

#include "dsp/plugins/azaRMS.c"
#include "dsp/plugins/azaCubicLimiter.c"
#include "dsp/plugins/azaLookaheadLimiter.c"
#include "dsp/plugins/azaFilter.c"
#include "dsp/plugins/azaCompressor.c"
#include "dsp/plugins/azaGate.c"
#include "dsp/plugins/azaDelay.c"
#include "dsp/plugins/azaDelayDynamic.c"
#include "dsp/plugins/azaReverb.c"
#include "dsp/plugins/azaSampler.c"
#include "dsp/plugins/azaSpatialize.c"
#include "dsp/plugins/azaMonitorSpectrum.c"