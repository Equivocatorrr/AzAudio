/*
	File: types.c
	Author: Philip Haynes
*/

#include "types.h"
#include "gui.h"

#include "../math.h"
#include "AzAudio/easing.h"


void azagRectFitOnScreen(azagRect *rect) {
	// NOTE: The auto-scroll thing works for context menus, but doesn't help with tooltips in the general case.
	//       That probably doesn't really matter though because tooltips are supposed to be small, and the auto-scroll
	//       only applies to rather extreme cases. The important thing is that we don't crash due to the assert in azaClampf.
	azaVec2 size = azagGetScreenSize();
	if (rect->w <= size.x) {
		rect->x = azaClampf(rect->x, 0.0f, size.x - rect->w);
	} else {
		// Auto-scroll based on mouse position
		float overwidth = rect->w - size.x;
		float mouseProgress = azaEaseCosineInOut(azaClampf((azagMousePosition().x / size.x - 0.25f) * 2.0f, 0.0f, 1.0f));
		rect->x = -overwidth * mouseProgress;
	}
	if (rect->h <= size.y) {
		rect->y = azaClampf(rect->y, 0.0f, size.y - rect->h);
	} else {
		// Auto-scroll based on mouse position
		float overheight = rect->h - size.y;
		float mouseProgress = azaEaseCosineInOut(azaClampf((azagMousePosition().y / size.y - 0.25f) * 2.0f, 0.0f, 1.0f));
		rect->y = -overheight * mouseProgress;
	}
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
