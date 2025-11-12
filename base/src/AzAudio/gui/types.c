/*
	File: types.c
	Author: Philip Haynes
*/

#include "types.h"
#include "gui.h"

#include "../math.h"



void azagRectFitOnScreen(azagRect *rect) {
	azagPoint size = azagGetScreenSize();
	rect->x = AZA_CLAMP(rect->x, 0, size.x - rect->w);
	rect->y = AZA_CLAMP(rect->y, 0, size.y - rect->h);
}



azagColor azagMakeColorHSVAf(float hue, float saturation, float value, float alpha) {
	float r, g, b;
	int section = (int)(hue * 6.0f);
	float fraction = hue * 6.0f - (float)section;
	section = azaWrapi(section, 6);
	switch (section) {
		case 0:
			r = (1.0f)*value;
			g = azaLerpf(1.0f, fraction, saturation)*value;
			b = 1.0f - saturation;
			break;
		case 1:
			r = azaLerpf(1.0f, 1.0f - fraction, saturation)*value;
			g = (1.0f)*value;
			b = 1.0f - saturation;
			break;
		case 2:
			r = 1.0f - saturation;
			g = (1.0f)*value;
			b = azaLerpf(1.0f, fraction, saturation)*value;
			break;
		case 3:
			r = 1.0f - saturation;
			g = azaLerpf(1.0f, 1.0f - fraction, saturation)*value;
			b = (1.0f)*value;
			break;
		case 4:
			r = azaLerpf(1.0f, fraction, saturation)*value;
			g = 1.0f - saturation;
			b = (1.0f)*value;
			break;
		case 5:
			r = (1.0f)*value;
			g = 1.0f - saturation;
			b = azaLerpf(1.0f, 1.0f - fraction, saturation)*value;
			break;
	}
	return azagMakeColorf(r, g, b, alpha);
}



azagColor azagColorSaturate(azagColor base, float saturation) {
	int high = AZA_MAX(AZA_MAX(base.r, base.g), base.b);
	int low = AZA_MIN(AZA_MIN(base.r, base.g), base.b);
	if (high == low) return base;
	int newLow = (int)((float)high * (1.0f - saturation));
	azagColor result = {
		(uint8_t)(((int)base.r - low) * (high - newLow) / (high - low) + newLow),
		(uint8_t)(((int)base.g - low) * (high - newLow) / (high - low) + newLow),
		(uint8_t)(((int)base.b - low) * (high - newLow) / (high - low) + newLow),
		base.a,
	};
	return result;
}

azagColor azagColorValue(azagColor base, float value) {
	int currentValue = AZA_MAX(AZA_MAX(base.r, base.g), base.b);
	azagColor result = {
		(uint8_t)((int)base.r * (int)(value*255.0f) / currentValue),
		(uint8_t)((int)base.g * (int)(value*255.0f) / currentValue),
		(uint8_t)((int)base.b * (int)(value*255.0f) / currentValue),
		base.a,
	};
	return result;
}

azagColor azagColorSatVal(azagColor base, float saturation, float value) {
	return azagColorSaturate(azagColorValue(base, value), saturation);
}
