/* LIBRARIES */
#include "pico/scanvideo/composable_scanline.h"
#include "pico/stdlib.h"
#include "pico/sync.h"
#include "PicoDraw.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>


/* VARIABLES */
static volatile uint64_t LastFrameTimeUS = 0;
static volatile bool BackbufReady = false;
static const scanvideo_mode_t* DisplayMode = NULL;
static uint16_t* FrontBuffer = NULL;
static uint16_t* BackBuffer = NULL;


/* FUNCTIONS */
// Gets the total size of the heap
uint32_t GetTotalHeap(void)
{
   extern char __StackLimit, __bss_end__;
   
   return &__StackLimit  - &__bss_end__;
}

// Gets the amount of free memory in the heap
uint32_t GetFreeHeap(void)
{
   struct mallinfo m = mallinfo();

   return GetTotalHeap() - m.uordblks;
}

// Returns the size of the framebuffer in bytes
static inline size_t GetFBSize(void)
{
    return (size_t)DisplayMode->width * (size_t)DisplayMode->height * sizeof(uint16_t);
}

// Figures out how long one frame takes to draw at the current refresh rate
static inline uint32_t FramePeriodUS(void)
{
    // Default to 60Hz
    if (!DisplayMode || !DisplayMode->default_timing)
        return 16666;

    const scanvideo_timing_t* DSPTiming = DisplayMode->default_timing;
    uint32_t TotalPixels = DSPTiming->h_total * DSPTiming->v_total;
    float RefreshRate = (float)DSPTiming->clock_freq / (float)TotalPixels;

    // Return the calculated microseconds
    return (uint32_t)(1e6f / RefreshRate + 0.5f);
}

const scanvideo_mode_t* GetDisplayMode(void)
{
    return DisplayMode;
}

// Determines if the framebuffer is allowed to be written to
// NOTE: Microseconds are used instead of milliseconds in order to prevent stuttering
bool DisplayCanDraw(void)
{
    if (!DisplayMode)
        return false;

    // Check if enough time has passed to allow the next frame to be drawn
    uint64_t CurrentTime = to_us_since_boot(get_absolute_time());
    uint32_t DeltaTimeUS = FramePeriodUS();

    if (CurrentTime - LastFrameTimeUS >= DeltaTimeUS)
    {
        LastFrameTimeUS = CurrentTime;
        return true;
    }

    return false;
}

// Attempts to start the next display frame
bool DisplayBeginDraw(void)
{
    return DisplayCanDraw() == true && BackbufReady == false;
}

// Lets the system know that drawing has finished
void DisplayEndDraw(void)
{
    BackbufReady = true;
}

// Sets a pixel at framebuffer (X,Y) to an RGB565 color
void SetPixel(unsigned X, unsigned Y, uint16_t Color)
{
    if (!BackBuffer || !DisplayMode)
        return;

    if (X >= DisplayMode->width || Y >= DisplayMode->height)
        return;
        
    BackBuffer[Y * DisplayMode->width + X] = Color;
}

// Draws a rectangle at (X,Y) with the specified dimensions and RGB565 color
void DrawRectangle(int X, int Y, int Width, int Height, uint16_t Color)
{
    if (!BackBuffer || !DisplayMode)
        return;

    // Figure out where the four courners of the rectangle are
    int X0 = X < 0 ? 0 : X;
    int Y0 = Y < 0 ? 0 : Y;
    int X1 = X + Width;
    int Y1 = Y + Height;

    // Make sure the rectangle's size is not negative and is within the display frame
    if (X1 > (int)DisplayMode->width)
        X1 = DisplayMode->width;
    
    if (Y1 > (int)DisplayMode->height)
        Y1 = DisplayMode->height;
    
    if (X0 >= X1 || Y0 >= Y1)
        return;

    // Calculate the pointer to the correct framebuffer coordinates (find the starting pixel)
    uint16_t* FBPtr = &BackBuffer[Y0 * DisplayMode->width + X0];

    // Draw the rectangle from top to bottom, row by row
    for (int RectRow = 0; RectRow < (Y1 - Y0); RectRow++)
    {
        // Fill the line in the framebuffer
        uint16_t* LinePointer = FBPtr;

        for (int RectCol = 0; RectCol < (X1 - X0); RectCol++)
            *LinePointer++ = Color;

        // Move the framebuffer pointer to the start of the next row
        FBPtr += DisplayMode->width;
    }
}

// Draws a RGB565 colored line between two 2D points
// NOTE: Uses a fast path approach for horizontal/vertical lines, and uses the Bresenham line algorithm otherwise
void DrawLine(int X0, int Y0, int X1, int Y1, uint16_t Color)
{
    if (!BackBuffer || !DisplayMode)
        return;

    // Clamp the line's endpoints so they stay within the display frame
    if (X0 < 0) X0 = 0;
    if (X1 < 0) X1 = 0;
    if (Y0 < 0) Y0 = 0;
    if (Y1 < 0) Y1 = 0;
    if (X0 >= (int)DisplayMode->width) X0 = DisplayMode->width - 1;
    if (X1 >= (int)DisplayMode->width) X1 = DisplayMode->width - 1;
    if (Y0 >= (int)DisplayMode->height) Y0 = DisplayMode->height - 1;
    if (Y1 >= (int)DisplayMode->height) Y1 = DisplayMode->height - 1;

    // See how much the X and Y coordinates change between the start and end points
    int XChange = X1 - X0;
    int YChange = Y1 - Y0;

    // Fast horizontal line
    if (YChange == 0)
    {
        if (XChange < 0)
        {
            int tmp = X0;
            X0 = X1;
            X1 = tmp;
        }
        
        // Fill the row (start pixel -> start pixel + line length)
        uint16_t* FBRow = &BackBuffer[Y0 * DisplayMode->width + X0];

        for (int X = 0; X <= X1 - X0; X++)
            FBRow[X] = Color;

        return;
    }

    // Fast vertical line
    if (XChange == 0)
    {
        if (YChange < 0)
        {
            int tmp = Y0;
            Y0 = Y1;
            Y1 = tmp;
        }

        uint16_t* PixelPointer = &BackBuffer[Y0 * DisplayMode->width + X0];

        for (int Y = Y0; Y <= Y1; Y++)
        {
            // Change the pixel and move to the next row
            *PixelPointer = Color;
            PixelPointer += DisplayMode->width;
        }

        return;
    }

    // Use the Bresenham line algorithm for other cases where the line is not perfectly horizontal or vertical:
    //      DX = horizontal distance between start and end points
    //      DY = negative vertical distance between start and end points
    //      SX = step in X: +1 means move right, -1 means move left
    //      SY = step in Y: +1 means move down, -1 means move up
    //      Error = decision variable that determines when to move along Y vs X while drawing the line
    int DX = abs(XChange);
    int DY = -abs(YChange);
    int SX = (X0 < X1) ? 1 : -1;
    int SY = (Y0 < Y1) ? 1 : -1;
    int Error = DX + DY;

    // Draw the line
    while (true)
    {
        // Put the pixel in the framebuffer
        BackBuffer[Y0 * DisplayMode->width + X0] = Color;

        // Stop once we've drawn all the pixels in the line's size
        if (X0 == X1 && Y0 == Y1)
            break;

        // Decide whether to step in X, Y, or both based on the accumulated error
        int DoubleError = 2 * Error;

        if (DoubleError >= DY)
        {
            Error += DY;
            X0 += SX;
        }

        if (DoubleError <= DX)
        {
            Error += DX;
            Y0 += SY;
        }
    }
}

// Draw a line from the framebuffer by converting it into a display scanline
// NOTE: The first pixel if the scanline must never be 0x0000 (full black), as the display engine treats this as "skip this scanline"
// NOTE: COMPOSABLE_RAW_RUN must always start at (0,0) on the screen, otherwise the PIO doesn't know how to handle it and outputs nothing
static void DrawScanlineFromFramebuffer(scanvideo_scanline_buffer_t* Framebuffer, uint FBRow)
{
    // Store pointers to the framebuffer row and scanline buffer
    uint16_t* FBRowPtr = FrontBuffer + FBRow * DisplayMode->width;
    uint16_t* SCBufferPtr = (uint16_t*)Framebuffer->data;

    // Tell the display engine's PIO program what mode to use
    *SCBufferPtr++ = COMPOSABLE_RAW_RUN;
    *SCBufferPtr++ = DisplayMode->width;

    // Make sure the first pixel placeholder is not 0x0000 (full black)
    *SCBufferPtr++ = PICO_SCANVIDEO_PIXEL_FROM_RGB5(0, 0, 1);

    // Set the scanline pixel by using the framebuffer pixel (RGB555)
    for (uint32_t FBColumn = 0; FBColumn < DisplayMode->width; FBColumn++)
    {
        // Extract the RGB 5-bit components from the 16 bit uint and write the color to the scanline
        *SCBufferPtr++ = *FBRowPtr++;
    }

    // Let the display engine know that the scanline has ended
    // NOTE: COMPOSABLE_EOL_SKIP_ALIGN tells the display engine to skip to the alignment stage
    *SCBufferPtr++ = COMPOSABLE_EOL_SKIP_ALIGN;
    *SCBufferPtr++ = 0;

    // Tell the display engine how much data was written to the scanline
    Framebuffer->data_used = ((uint32_t*)SCBufferPtr) - ((uint32_t*)Framebuffer->data);
    Framebuffer->status = SCANLINE_OK;
}

// Output the display signal to the monitor
void StartRendering(void)
{
    while(true)
    {
        // Start the next scanline and figure out which line that actually was
        scanvideo_scanline_buffer_t *ScanlineBuffer = scanvideo_begin_scanline_generation(true);
        uint Scanline = scanvideo_scanline_number(ScanlineBuffer->scanline_id);

        // If we're at the bottom of the screen, swap the front and back framebuffers when ready
        if(Scanline == DisplayMode->height - 1)
        {
            if(BackbufReady == true)
            {
                uint16_t* TMPBuf = FrontBuffer;
                FrontBuffer = BackBuffer;
                BackBuffer = TMPBuf;
                BackbufReady = false;
            }
        }

        // Make sure only visible scanlines are drawn, and tell the display engine that the scanline
        // is ready to be output to the monitor if we aren't in the visible screen area
        if(Scanline < DisplayMode->height)
        {
            DrawScanlineFromFramebuffer(ScanlineBuffer, Scanline);
        }
        else
        {
            ScanlineBuffer->data_used = 0;
            ScanlineBuffer->status = SCANLINE_OK;
        }

        // Let the display engine know that this scanline is ready to be rendered
        scanvideo_end_scanline_generation(ScanlineBuffer);
    }
}

// Set up scanvideo and the frmaebuffers
// NOTE: Return codes are defined as:
//      0 -> Init finished with no errors
//      1 -> Fremabuffer allocation failed
int DisplayInit(const scanvideo_mode_t* VGADisplayMode)
{
    // Assign the VGA mode and get the size of the framebuffer
    DisplayMode = VGADisplayMode;
    size_t FBSize = GetFBSize();

    // Make sure the framebuffer will fit in our memory
    if (FBSize == 0 || FBSize * 2 > (size_t)GetFreeHeap())
    {
        return -1;
    }

    // Allocate framebuffer(s)
    FrontBuffer = malloc(FBSize);
    BackBuffer = malloc(FBSize);

    // Make sure the framebuffers are actually allocated
    if(!FrontBuffer || !BackBuffer)
    {
        if(FrontBuffer)
            free(FrontBuffer);
            
        if(BackBuffer)
            free(BackBuffer);
            
        return -1;
    }

    // Display an initial splash image
    DisplayBeginDraw();
    DrawRectangle(0, 0, DisplayMode->width, DisplayMode->height, 0x0000);
    DrawLine(0, 0, 32, 0, 0xFFE0);
    DrawLine(32, 0, 16, 32, 0xFFE0);
    DrawLine(16, 32, 0, 0, 0xFFE0);
    DrawRectangle(0, 40, 32, 32, 0x001F);
    DrawRectangle(32, 40, 32, 32, 0x07E0);
    DrawRectangle(64, 40, 32, 32, 0xF800);
    DrawRectangle(96, 40, 32, 32, 0xF81F);
    DrawRectangle(128, 40, 32, 32, 0xFFFF);
    DisplayEndDraw();

    // Set up scanvideo
    scanvideo_setup(DisplayMode);
    scanvideo_timing_enable(true);
    LastFrameTimeUS = to_us_since_boot(get_absolute_time());
    
    // Return 0 to indicate a successful initialization
    return 0;
}
