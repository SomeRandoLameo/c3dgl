/* Host stand-in for <citro3d.h>: the statistics calls of the example programs (bench/README.md). */
#pragma once
#include <3ds.h>
static inline float C3D_GetProcessingTime(void) { return 0; }
static inline float C3D_GetDrawingTime(void) { return 0; }
static inline float C3D_GetCmdBufUsage(void) { return 0; }
