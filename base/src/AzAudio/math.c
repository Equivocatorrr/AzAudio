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
	float windowT = azaClampf(x / radius, -1.0f, 1.0f);
	return azaSincf(x) * azaWindowHannf(windowT * 0.5f + 0.5f);
}

float azaSincHalfSinef(float x, float radius) {
	float windowT = azaClampf(x / radius, -1.0f, 1.0f);
	float c = cosf(windowT * AZA_PI * 0.5f);
	return azaSincf(x) * c;
}

float azaSincBlackmanf(float x, float radius) {
	float windowT = azaClampf(x / radius, -1.0f, 1.0f);
	return azaSincf(x) * azaWindowBlackmanf(windowT * 0.5f + 0.5f);
}

float azaLanczosf(float x, float radius) {
	float windowT = azaClampf(x / radius, -1.0f, 1.0f);
	return azaSincf(x) * azaSincf(windowT);
}

float azaLUTSincf(float x) {
	if (x == 0)
		return 1.0f;
	return azaOscSine(x * 0.5f) / (x * AZA_PI);
}

float azaLUTSincHannf(float x, float radius) {
	float windowT = azaClampf(x / radius, -1.0f, 1.0f);
	float c = azaOscCosine(windowT * 0.25f);
	return azaLUTSincf(x) * c*c;
}

float azaLUTLanczosf(float x, float radius) {
	float windowT = azaClampf(x / radius, -1.0f, 1.0f);
	return azaLUTSincf(x) * azaLUTSincf(windowT);
}

float azaCubicf(float a, float b, float c, float d, float x) {
	return b + 0.5f * x * (c - a + x * (2 * a - 5 * b + 4 * c - d + x * (3 * (b - c) + d - a)));
}