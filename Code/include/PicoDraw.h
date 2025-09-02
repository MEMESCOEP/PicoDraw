#ifndef PICODRAW_H
#define PICODRAW_H

/* LIBRARIES */
#include "pico/scanvideo.h"
#include <stdbool.h>
#include <stdint.h>


/* FUNCTIONS */
// ----- INIT & FRAME -----
const scanvideo_mode_t* GetDisplayMode(void);
void DisplayEndDraw(void);
void StartRendering(void);
bool DisplayBeginDraw(void);
bool DisplayCanDraw(void);
int DisplayInit(const scanvideo_mode_t *VGADisplayMode);

// ----- DRAWING -----
void DrawRectangle(int X, int Y, int Width, int Height, uint16_t Color);
void DrawLine(int X0, int Y0, int X1, int Y1, uint16_t Color);
void SetPixel(unsigned X, unsigned Y, uint16_t Color);

#endif 