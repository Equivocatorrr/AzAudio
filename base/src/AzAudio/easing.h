/*
	File: easing.h
	Author: Philip Haynes
	Easing functions for parameterizing interpolations. All easing functions are defined to be 0 at t=0 and 1 at t=1.
*/

#ifndef AZAUDIO_EASING_H
#define AZAUDIO_EASING_H

#ifdef __cplusplus
extern "C" {
#endif

typedef float (*fp_azaEase_t)(float t);

// has a slope of 1 everywhere
float azaEaseLinear(float t);

// starts fast, ends slow
// starts with a slope of pi/2, ends with a slope of 0
float azaEaseCosineIn(float t);

// starts slow, ends fast
// starts with a slope of 0, ends with a slope of pi/2
float azaEaseCosineOut(float t);

// S curve, starts and ends slow
// start has a slope of 0, midpoint has a slope of pi/2 and end has a slope of 0
float azaEaseCosineInOut(float t);


#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_EASING_H