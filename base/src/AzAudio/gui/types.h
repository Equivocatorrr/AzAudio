/*
	File: types.h
	Author: Philip Haynes
*/

#ifndef AZAUDIO_TYPES_H
#define AZAUDIO_TYPES_H

#include "../aza_c_std.h"

#ifdef __cplusplus
extern "C" {
#endif



typedef struct azagPoint {
	int x, y;
} azagPoint;

static inline azagPoint azagPointAdd(azagPoint lhs, azagPoint rhs) {
	return AZA_CLITERAL(azagPoint) { lhs.x + rhs.x, lhs.y + rhs.y };
}

static inline azagPoint azagPointSub(azagPoint lhs, azagPoint rhs) {
	return AZA_CLITERAL(azagPoint) { lhs.x - rhs.x, lhs.y - rhs.y };
}

static inline azagPoint azagPointMul(azagPoint lhs, azagPoint rhs) {
	return AZA_CLITERAL(azagPoint) { lhs.x * rhs.x, lhs.y * rhs.y };
}

static inline azagPoint azagPointDiv(azagPoint lhs, azagPoint rhs) {
	return AZA_CLITERAL(azagPoint) { lhs.x / rhs.x, lhs.y / rhs.y };
}

static inline azagPoint azagPointMulScalar(azagPoint point, int scalar) {
	return AZA_CLITERAL(azagPoint) { point.x * scalar, point.y * scalar };
}

static inline azagPoint azagPointDivScalar(azagPoint point, int scalar) {
	return AZA_CLITERAL(azagPoint) { point.x / scalar, point.y / scalar };
}



typedef struct azagRect {
	union {
		struct { int x, y; };
		azagPoint xy;
	};
	union {
		struct { int w, h; };
		azagPoint size;
	};
} azagRect;

static inline bool azagPointInRect(azagRect rect, azagPoint point) {
	return (point.x >= rect.x && point.y >= rect.y && point.x <= rect.x+rect.w && point.y <= rect.y+rect.h);
}

static inline void azagRectShrinkAll(azagRect *rect, int m) {
	rect->x += m;
	rect->y += m;
	rect->w -= m*2;
	rect->h -= m*2;
}

static inline void azagRectShrinkAllXY(azagRect *rect, azagPoint xy) {
	rect->x += xy.x;
	rect->y += xy.y;
	rect->w -= xy.x*2;
	rect->h -= xy.y*2;
}

static inline void azagRectShrinkAllH(azagRect *rect, int m) {
	rect->x += m;
	rect->w -= m*2;
}

static inline void azagRectShrinkAllV(azagRect *rect, int m) {
	rect->y += m;
	rect->h -= m*2;
}

static inline void azagRectShrinkTop(azagRect *rect, int h) {
	rect->y += h;
	rect->h -= h;
}

static inline void azagRectShrinkBottom(azagRect *rect, int h) {
	rect->h -= h;
}

static inline void azagRectShrinkLeft(azagRect *rect, int w) {
	rect->x += w;
	rect->w -= w;
}

static inline void azagRectShrinkRight(azagRect *rect, int w) {
	rect->w -= w;
}

void azagRectFitOnScreen(azagRect *rect);



typedef union azagColor {
	struct { uint8_t r, g, b, a; };
	uint8_t rgba[4];
	uint32_t data;
} azagColor;
// Color component values in the range 0 to 255
static inline azagColor azagMakeColoru8(uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
	return AZA_CLITERAL(azagColor) {
		{ red, green, blue, alpha }
	};
}
// Color component values in the range 0 to 1
static inline azagColor azagMakeColorf(float red, float green, float blue, float alpha) {
	return AZA_CLITERAL(azagColor) {
		{
			(uint8_t)(red * 255.0f),
			(uint8_t)(green * 255.0f),
			(uint8_t)(blue * 255.0f),
			(uint8_t)(alpha * 255.0f),
		},
	};
}
// all values are in the range 0 to 1
azagColor azagMakeColorHSVAf(float hue, float saturation, float value, float alpha);

azagColor azagColorSaturate(azagColor base, float saturation);
azagColor azagColorValue(azagColor base, float value);
azagColor azagColorSatVal(azagColor base, float saturation, float value);



// Scales referring to font sizes specified in the theme
typedef enum azagTextScale {
	AZAG_TEXT_SCALE_TEXT   = 0, // Normal small text
	AZAG_TEXT_SCALE_HEADER = 1, // Large header text
	AZAG_TEXT_SCALE_ENUM_COUNT
} azagTextScale;



// Mouse input



typedef enum azagMouseButton {
	AZAG_MOUSE_BUTTON_LEFT   = 0,
	AZAG_MOUSE_BUTTON_RIGHT  = 1,
	AZAG_MOUSE_BUTTON_MIDDLE = 2,
	AZAG_MOUSE_BUTTON_X1     = 3,
	AZAG_MOUSE_BUTTON_X2     = 4,
} azagMouseButton;

// Mouse depth values for input culling depending on certain UI states.
// Certain operations will set the global mouse depth.
// Mouse input functions must be passed a value that's >= to the global value in order to be read.
typedef enum azagMouseDepth {
	AZAG_MOUSE_DEPTH_BASE = 0,
	AZAG_MOUSE_DEPTH_CONTEXT_MENU = 1,
	AZAG_MOUSE_DEPTH_TEXT_INPUT = 2,
	AZAG_MOUSE_DEPTH_DRAG = 3,
	AZAG_MOUSE_DEPTH_ALL = 1337,
} azagMouseDepth;

typedef enum azagMouseCursor {
	AZAG_MOUSE_CURSOR_DEFAULT           = 0,
	AZAG_MOUSE_CURSOR_ARROW             = 1,
	AZAG_MOUSE_CURSOR_IBEAM             = 2,
	AZAG_MOUSE_CURSOR_CROSSHAIR         = 3,
	AZAG_MOUSE_CURSOR_POINTING_HAND     = 4,
	AZAG_MOUSE_CURSOR_RESIZE_H          = 5,
	AZAG_MOUSE_CURSOR_RESIZE_V          = 6,
	AZAG_MOUSE_CURSOR_RESIZE_DIAG_TL2BR = 7,
	AZAG_MOUSE_CURSOR_RESIZE_DIAG_TR2BL = 8,
	AZAG_MOUSE_CURSOR_RESIZE_OMNI       = 9,
	AZAG_MOUSE_CURSOR_NOT_ALLOWED       = 10,
} azagMouseCursor;



// Keyboard input



// KeyCodes from AzCore
typedef enum azagKeyCode {
	AZAG_KEY_NONE                = 0x00, // No key pressed
	AZAG_KEY_ERR                 = 0x01, // Keyboard Error Roll Over - used for all slots if too many keys are pressed ("Phantom key")
	AZAG_KEY_A                   = 0x04, // Keyboard a and A
	AZAG_KEY_B                   = 0x05, // Keyboard b and B
	AZAG_KEY_C                   = 0x06, // Keyboard c and C
	AZAG_KEY_D                   = 0x07, // Keyboard d and D
	AZAG_KEY_E                   = 0x08, // Keyboard e and E
	AZAG_KEY_F                   = 0x09, // Keyboard f and F
	AZAG_KEY_G                   = 0x0a, // Keyboard g and G
	AZAG_KEY_H                   = 0x0b, // Keyboard h and H
	AZAG_KEY_I                   = 0x0c, // Keyboard i and I
	AZAG_KEY_J                   = 0x0d, // Keyboard j and J
	AZAG_KEY_K                   = 0x0e, // Keyboard k and K
	AZAG_KEY_L                   = 0x0f, // Keyboard l and L
	AZAG_KEY_M                   = 0x10, // Keyboard m and M
	AZAG_KEY_N                   = 0x11, // Keyboard n and N
	AZAG_KEY_O                   = 0x12, // Keyboard o and O
	AZAG_KEY_P                   = 0x13, // Keyboard p and P
	AZAG_KEY_Q                   = 0x14, // Keyboard q and Q
	AZAG_KEY_R                   = 0x15, // Keyboard r and R
	AZAG_KEY_S                   = 0x16, // Keyboard s and S
	AZAG_KEY_T                   = 0x17, // Keyboard t and T
	AZAG_KEY_U                   = 0x18, // Keyboard u and U
	AZAG_KEY_V                   = 0x19, // Keyboard v and V
	AZAG_KEY_W                   = 0x1a, // Keyboard w and W
	AZAG_KEY_X                   = 0x1b, // Keyboard x and X
	AZAG_KEY_Y                   = 0x1c, // Keyboard y and Y
	AZAG_KEY_Z                   = 0x1d, // Keyboard z and Z

	AZAG_KEY_1                   = 0x1e, // Keyboard 1 and !
	AZAG_KEY_2                   = 0x1f, // Keyboard 2 and @
	AZAG_KEY_3                   = 0x20, // Keyboard 3 and #
	AZAG_KEY_4                   = 0x21, // Keyboard 4 and $
	AZAG_KEY_5                   = 0x22, // Keyboard 5 and %
	AZAG_KEY_6                   = 0x23, // Keyboard 6 and ^
	AZAG_KEY_7                   = 0x24, // Keyboard 7 and &
	AZAG_KEY_8                   = 0x25, // Keyboard 8 and *
	AZAG_KEY_9                   = 0x26, // Keyboard 9 and (
	AZAG_KEY_0                   = 0x27, // Keyboard 0 and )

	AZAG_KEY_ENTER               = 0x28, // Keyboard Return (ENTER)
	AZAG_KEY_ESC                 = 0x29, // Keyboard ESCAPE
	AZAG_KEY_BACKSPACE           = 0x2a, // Keyboard DELETE (Backspace)
	AZAG_KEY_TAB                 = 0x2b, // Keyboard Tab
	AZAG_KEY_SPACE               = 0x2c, // Keyboard Spacebar
	AZAG_KEY_MINUS               = 0x2d, // Keyboard - and _
	AZAG_KEY_EQUAL               = 0x2e, // Keyboard = and +
	AZAG_KEY_LEFTBRACE           = 0x2f, // Keyboard [ and {
	AZAG_KEY_RIGHTBRACE          = 0x30, // Keyboard ] and }
	AZAG_KEY_BACKSLASH           = 0x31, // Keyboard \ and |
	AZAG_KEY_HASHTILDE           = 0x32, // Keyboard Non-US # and ~
	AZAG_KEY_SEMICOLON           = 0x33, // Keyboard ; and :
	AZAG_KEY_APOSTROPHE          = 0x34, // Keyboard ' and "
	AZAG_KEY_GRAVE               = 0x35, // Keyboard ` and ~
	AZAG_KEY_COMMA               = 0x36, // Keyboard , and <
	AZAG_KEY_DOT                 = 0x37, // Keyboard . and >
	AZAG_KEY_SLASH               = 0x38, // Keyboard / and ?
	AZAG_KEY_CAPSLOCK            = 0x39, // Keyboard Caps Lock

	AZAG_KEY_F1                  = 0x3a, // Keyboard F1
	AZAG_KEY_F2                  = 0x3b, // Keyboard F2
	AZAG_KEY_F3                  = 0x3c, // Keyboard F3
	AZAG_KEY_F4                  = 0x3d, // Keyboard F4
	AZAG_KEY_F5                  = 0x3e, // Keyboard F5
	AZAG_KEY_F6                  = 0x3f, // Keyboard F6
	AZAG_KEY_F7                  = 0x40, // Keyboard F7
	AZAG_KEY_F8                  = 0x41, // Keyboard F8
	AZAG_KEY_F9                  = 0x42, // Keyboard F9
	AZAG_KEY_F10                 = 0x43, // Keyboard F10
	AZAG_KEY_F11                 = 0x44, // Keyboard F11
	AZAG_KEY_F12                 = 0x45, // Keyboard F12

	AZAG_KEY_SYSRQ               = 0x46, // Keyboard Print Screen
	AZAG_KEY_SCROLLLOCK          = 0x47, // Keyboard Scroll Lock
	AZAG_KEY_PAUSE               = 0x48, // Keyboard Pause
	AZAG_KEY_INSERT              = 0x49, // Keyboard Insert
	AZAG_KEY_HOME                = 0x4a, // Keyboard Home
	AZAG_KEY_PAGEUP              = 0x4b, // Keyboard Page Up
	AZAG_KEY_DELETE              = 0x4c, // Keyboard Delete Forward
	AZAG_KEY_END                 = 0x4d, // Keyboard End
	AZAG_KEY_PAGEDOWN            = 0x4e, // Keyboard Page Down
	AZAG_KEY_RIGHT               = 0x4f, // Keyboard Right Arrow
	AZAG_KEY_LEFT                = 0x50, // Keyboard Left Arrow
	AZAG_KEY_DOWN                = 0x51, // Keyboard Down Arrow
	AZAG_KEY_UP                  = 0x52, // Keyboard Up Arrow

	AZAG_KEY_NUMLOCK             = 0x53, // Keyboard Num Lock and Clear
	AZAG_KEY_KPSLASH             = 0x54, // Keypad /
	AZAG_KEY_KPASTERISK          = 0x55, // Keypad *
	AZAG_KEY_KPMINUS             = 0x56, // Keypad -
	AZAG_KEY_KPPLUS              = 0x57, // Keypad +
	AZAG_KEY_KPENTER             = 0x58, // Keypad ENTER
	AZAG_KEY_KP1                 = 0x59, // Keypad 1 and End
	AZAG_KEY_KP2                 = 0x5a, // Keypad 2 and Down Arrow
	AZAG_KEY_KP3                 = 0x5b, // Keypad 3 and PageDn
	AZAG_KEY_KP4                 = 0x5c, // Keypad 4 and Left Arrow
	AZAG_KEY_KP5                 = 0x5d, // Keypad 5
	AZAG_KEY_KP6                 = 0x5e, // Keypad 6 and Right Arrow
	AZAG_KEY_KP7                 = 0x5f, // Keypad 7 and Home
	AZAG_KEY_KP8                 = 0x60, // Keypad 8 and Up Arrow
	AZAG_KEY_KP9                 = 0x61, // Keypad 9 and Page Up
	AZAG_KEY_KP0                 = 0x62, // Keypad 0 and Insert
	AZAG_KEY_KPDOT               = 0x63, // Keypad . and Delete

	AZAG_KEY_102ND               = 0x64, // Keyboard Non-US \ and |
	AZAG_KEY_COMPOSE             = 0x65, // Keyboard Application
	AZAG_KEY_POWER               = 0x66, // Keyboard Power
	AZAG_KEY_KPEQUAL             = 0x67, // Keypad =

	AZAG_KEY_F13                 = 0x68, // Keyboard F13
	AZAG_KEY_F14                 = 0x69, // Keyboard F14
	AZAG_KEY_F15                 = 0x6a, // Keyboard F15
	AZAG_KEY_F16                 = 0x6b, // Keyboard F16
	AZAG_KEY_F17                 = 0x6c, // Keyboard F17
	AZAG_KEY_F18                 = 0x6d, // Keyboard F18
	AZAG_KEY_F19                 = 0x6e, // Keyboard F19
	AZAG_KEY_F20                 = 0x6f, // Keyboard F20
	AZAG_KEY_F21                 = 0x70, // Keyboard F21
	AZAG_KEY_F22                 = 0x71, // Keyboard F22
	AZAG_KEY_F23                 = 0x72, // Keyboard F23
	AZAG_KEY_F24                 = 0x73, // Keyboard F24

	AZAG_KEY_OPEN                = 0x74, // Keyboard Execute
	AZAG_KEY_HELP                = 0x75, // Keyboard Help
	AZAG_KEY_PROPS               = 0x76, // Keyboard Menu
	AZAG_KEY_FRONT               = 0x77, // Keyboard Select
	AZAG_KEY_STOP                = 0x78, // Keyboard Stop
	AZAG_KEY_AGAIN               = 0x79, // Keyboard Again
	AZAG_KEY_UNDO                = 0x7a, // Keyboard Undo
	AZAG_KEY_CUT                 = 0x7b, // Keyboard Cut
	AZAG_KEY_COPY                = 0x7c, // Keyboard Copy
	AZAG_KEY_PASTE               = 0x7d, // Keyboard Paste
	AZAG_KEY_FIND                = 0x7e, // Keyboard Find
	AZAG_KEY_MUTE                = 0x7f, // Keyboard Mute
	AZAG_KEY_VOLUMEUP            = 0x80, // Keyboard Volume Up
	AZAG_KEY_VOLUMEDOWN          = 0x81, // Keyboard Volume Down
// 0x82  Keyboard Locking Caps Lock
// 0x83  Keyboard Locking Num Lock
// 0x84  Keyboard Locking Scroll Lock
	AZAG_KEY_KPCOMMA             = 0x85,
// 0x86  Keypad Equal Sign
	AZAG_KEY_RO                  = 0x87, // Keyboard International1
	AZAG_KEY_KATAKANAHIRAGANA    = 0x88, // Keyboard International2
	AZAG_KEY_YEN                 = 0x89, // Keyboard International3
	AZAG_KEY_HENKAN              = 0x8a, // Keyboard International4
	AZAG_KEY_MUHENKAN            = 0x8b, // Keyboard International5
	AZAG_KEY_KPJPCOMMA           = 0x8c, // Keyboard International6
// 0x8d  Keyboard International7
// 0x8e  Keyboard International8
// 0x8f  Keyboard International9
	AZAG_KEY_HANGEUL             = 0x90, // Keyboard LANG1
	AZAG_KEY_HANJA               = 0x91, // Keyboard LANG2
	AZAG_KEY_KATAKANA            = 0x92, // Keyboard LANG3
	AZAG_KEY_HIRAGANA            = 0x93, // Keyboard LANG4
	AZAG_KEY_ZENKAKUHANKAKU      = 0x94, // Keyboard LANG5
// 0x95  Keyboard LANG6
// 0x96  Keyboard LANG7
// 0x97  Keyboard LANG8
// 0x98  Keyboard LANG9
// 0x99  Keyboard Alternate Erase
// 0x9a  Keyboard SysReq/Attention
// 0x9b  Keyboard Cancel
// 0x9c  Keyboard Clear
// 0x9d  Keyboard Prior
// 0x9e  Keyboard Return
// 0x9f  Keyboard Separator
// 0xa0  Keyboard Out
// 0xa1  Keyboard Oper
// 0xa2  Keyboard Clear/Again
// 0xa3  Keyboard CrSel/Props
// 0xa4  Keyboard ExSel

// 0xb0  Keypad 00
// 0xb1  Keypad 000
// 0xb2  Thousands Separator
// 0xb3  Decimal Separator
// 0xb4  Currency Unit
// 0xb5  Currency Sub-unit
	AZAG_KEY_KPLEFTPAREN         = 0xb6, // Keypad (
	AZAG_KEY_KPRIGHTPAREN        = 0xb7, // Keypad )
// 0xb8  Keypad {
// 0xb9  Keypad }
// 0xba  Keypad Tab
// 0xbb  Keypad Backspace
// 0xbc  Keypad A
// 0xbd  Keypad B
// 0xbe  Keypad C
// 0xbf  Keypad D
// 0xc0  Keypad E
// 0xc1  Keypad F
// 0xc2  Keypad XOR
// 0xc3  Keypad ^
// 0xc4  Keypad %
// 0xc5  Keypad <
// 0xc6  Keypad >
// 0xc7  Keypad &
// 0xc8  Keypad &&
// 0xc9  Keypad |
// 0xca  Keypad ||
// 0xcb  Keypad :
// 0xcc  Keypad #
// 0xcd  Keypad Space
// 0xce  Keypad @
// 0xcf  Keypad !
// 0xd0  Keypad Memory Store
// 0xd1  Keypad Memory Recall
// 0xd2  Keypad Memory Clear
// 0xd3  Keypad Memory Add
// 0xd4  Keypad Memory Subtract
// 0xd5  Keypad Memory Multiply
// 0xd6  Keypad Memory Divide
// 0xd7  Keypad +/-
// 0xd8  Keypad Clear
// 0xd9  Keypad Clear Entry
// 0xda  Keypad Binary
// 0xdb  Keypad Octal
// 0xdc  Keypad Decimal
// 0xdd  Keypad Hexadecimal

// 0xdf  Unused
	AZAG_KEY_LEFTCTRL            = 0xe0, // Keyboard Left Control
	AZAG_KEY_LEFTSHIFT           = 0xe1, // Keyboard Left Shift
	AZAG_KEY_LEFTALT             = 0xe2, // Keyboard Left Alt
	AZAG_KEY_LEFTMETA            = 0xe3, // Keyboard Left GUI
	AZAG_KEY_RIGHTCTRL           = 0xe4, // Keyboard Right Control
	AZAG_KEY_RIGHTSHIFT          = 0xe5, // Keyboard Right Shift
	AZAG_KEY_RIGHTALT            = 0xe6, // Keyboard Right Alt
	AZAG_KEY_RIGHTMETA           = 0xe7, // Keyboard Right GUI

	AZAG_KEY_MEDIA_PLAYPAUSE     = 0xe8,
	AZAG_KEY_MEDIA_STOPCD        = 0xe9,
	AZAG_KEY_MEDIA_PREVIOUSSONG  = 0xea,
	AZAG_KEY_MEDIA_NEXTSONG      = 0xeb,
	AZAG_KEY_MEDIA_EJECTCD       = 0xec,
	AZAG_KEY_MEDIA_VOLUMEUP      = 0xed,
	AZAG_KEY_MEDIA_VOLUMEDOWN    = 0xee,
	AZAG_KEY_MEDIA_MUTE          = 0xef,
	AZAG_KEY_MEDIA_WWW           = 0xf0,
	AZAG_KEY_MEDIA_BACK          = 0xf1,
	AZAG_KEY_MEDIA_FORWARD       = 0xf2,
	AZAG_KEY_MEDIA_STOP          = 0xf3,
	AZAG_KEY_MEDIA_FIND          = 0xf4,
	AZAG_KEY_MEDIA_SCROLLUP      = 0xf5,
	AZAG_KEY_MEDIA_SCROLLDOWN    = 0xf6,
	AZAG_KEY_MEDIA_EDIT          = 0xf7,
	AZAG_KEY_MEDIA_SLEEP         = 0xf8,
	AZAG_KEY_MEDIA_COFFEE        = 0xf9,
	AZAG_KEY_MEDIA_REFRESH       = 0xfa,
	AZAG_KEY_MEDIA_CALC          = 0xfb,
// These codes are actually way beyond the scope of a u8, but I'm moving them here.
	AZAG_KEY_MEDIA_MAIL          = 0xfc,
	AZAG_KEY_MEDIA_FILE          = 0xfd,
} azagKeyCode;



#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_TYPES_H
