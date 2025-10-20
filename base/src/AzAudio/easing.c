/*
	File: easing.c
	Author: Philip Haynes
*/

#include "easing.h"

#include "math.h"

float azaEaseLinear(float t) {
	return t;
}

float azaEaseCosineIn(float t) {
	return sinf(t * (AZA_PI / 2.0f));
}

float azaEaseCosineOut(float t) {
	return 1.0f - cosf(t * (AZA_PI / 2.0f));
}

float azaEaseCosineInOut(float t) {
	return 0.5f * (1.0f - cosf(t * AZA_PI));
}