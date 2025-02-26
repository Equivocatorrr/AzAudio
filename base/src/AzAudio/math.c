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

float azaLanczosf(float x, float radius) {
	float c = cosf(x * AZA_PI * 0.5f / radius);
	return azaSincf(x) * c*c;
}

float azaCubicf(float a, float b, float c, float d, float x) {
	return b + 0.5f * x * (c - a + x * (2 * a - 5 * b + 4 * c - d + x * (3 * (b - c) + d - a)));
}