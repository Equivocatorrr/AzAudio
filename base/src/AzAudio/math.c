/*
	File: math.c
	Author: Philip Haynes
*/

#include "math.h"

float azaSincf(float x) {
	if (x == 0)
		return 1.0f;
	float temp = x * AZA_PI;
	return sinf(temp) / temp;
}

float azaSincHannf(float x, float radius) {
	return azaSincf(x) * azaWindowHannf(x / radius * 0.5f + 0.5f);
}

float azaSincHalfSinef(float x, float radius) {
	float c = cosf(x * AZA_PI * 0.5f / radius);
	return azaSincf(x) * c;
}

float azaSincBlackmanf(float x, float radius) {
	return azaSincf(x) * azaWindowBlackmanf(x / radius * 0.5f + 0.5f);
}

float azaLanczosf(float x, float radius) {
	return azaSincf(x) * azaSincf(x/radius);
}

float azaLUTSincf(float x) {
	if (x == 0)
		return 1.0f;
	return azaOscSine(x * 0.5f) / (x * AZA_PI);
}

float azaLUTSincHannf(float x, float radius) {
	float c = azaOscCosine(x * 0.25f / radius);
	return azaLUTSincf(x) * c*c;
}

float azaLUTLanczosf(float x, float radius) {
	return azaLUTSincf(x) * azaLUTSincf(x/radius);
}

float azaCubicf(float a, float b, float c, float d, float x) {
	return b + 0.5f * x * (c - a + x * (2 * a - 5 * b + 4 * c - d + x * (3 * (b - c) + d - a)));
}