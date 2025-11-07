/*
	File: platform.h
	Author: Philip Haynes
	Platform backend-specific interface.
*/

#ifndef AZAUDIO_GUI_PLATFORM_H
#define AZAUDIO_GUI_PLATFORM_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif



typedef uint32_t azagWindow;
enum { AZAG_WINDOW_INVALID = 0 };

azagWindow azagWindowCreate(int width, int height, const char *title);
void azagWindowDestroy(azagWindow window);

// Set the window that's used for drawing and input functions
// This is thread_local, so each thread can work with different windows at the same time
// NOTE: As long as Raylib is the only backend, this is just a stub for the future, as Raylib doesn't support opening multiple windows.
void azagWindowSetCurrent(azagWindow window);

void azagWindowSetAlwaysOnTop(bool alwaysOnTop);

// Implementer's note: backend needs to call `void azagOnGuiOpen()` once drawing is set up
// This function is not in this header. You must declare it in the implementation file.
int azagWindowOpen();
void azagWindowClose();

void azagWindowPollEvents();

// Read close button, returning true if the window should be closed
// Calls azagWindowPollEvents internally.
bool azagWindowShouldClose();

int azagGetScreenWidth();
int azagGetScreenHeight();
azagPoint azagGetScreenSize();



// Drawing



// Implementer's note: backend needs to call `void azagOnDrawBegin()` once drawing is set up
// This function is not in this header. You must declare it in the implementation file.
void azagBeginDrawing();
// Implementer's note: backend needs to call `void azagOnDrawEnd()` before actually finishing up drawing
// This function is not in this header. You must declare it in the implementation file.
void azagEndDrawing();

void azagClearBackground(azagColor color);

void azagSetScissor(azagRect rect);
void azagResetScissor();


void azagDrawLine(azagPoint posStart, azagPoint posEnd, azagColor color);

void azagDrawRect(azagRect rect, azagColor color);
void azagDrawRect(azagRect rect, azagColor color);
void azagDrawRectOutline(azagRect rect, azagColor color);
void azagDrawRectGradient(azagRect rect, azagColor topLeft, azagColor bottomLeft, azagColor topRight, azagColor bottomRight);
void azagDrawRectGradientV(azagRect rect, azagColor top, azagColor bottom);
void azagDrawRectGradientH(azagRect rect, azagColor left, azagColor right);



// Text



int azagTextWidth(const char *text, azagTextScale scale);
int azagTextHeight(const char *text, azagTextScale scale);
azagPoint azagTextSize(const char *text, azagTextScale scale);

// Text is aligned on the top left
void azagDrawText(const char *text, azagPoint position, azagTextScale textScale, azagColor color);



// Mouse input



azagPoint azagMousePosition();
float azagMouseWheelV();
float azagMouseWheelH();

// Base form that does no depth culling, just the actual input from the platform
bool azagMousePressed_base(azagMouseButton button);
bool azagMouseDown_base(azagMouseButton button);
bool azagMouseReleased_base(azagMouseButton button);



// Keyboard input



bool azagKeyRepeated(azagKeyCode key);
bool azagKeyPressed(azagKeyCode key);
bool azagKeyDown(azagKeyCode key);
bool azagKeyReleased(azagKeyCode key);

// returns the next queued unicode character, or 0 if the queue is empty.
// TODO: Generally speaking this is oversimplified for text input. We can keep this interface and add a better one later for IMEs and such.
uint32_t azagGetNextChar();




#ifdef __cplusplus
}
#endif

#endif // AZAUDIO_GUI_PLATFORM_H
