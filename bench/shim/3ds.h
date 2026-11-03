/* Host stand-in for <3ds.h>: just what the example programs and the comparison adapter use, so their
 * scene code builds on the host against the native renderer (bench/README.md). */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <mach/mach_time.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef volatile u32 vu32; typedef s32 Result; typedef u32 Handle;
typedef enum { GFX_TOP = 0, GFX_BOTTOM = 1 } gfxScreen_t;
typedef enum { GFX_LEFT = 0, GFX_RIGHT = 1 } gfx3dSide_t;
typedef struct { u8 *gfx; u16 asciiOffset; u16 numChars; } ConsoleFont;
typedef struct { ConsoleFont font; int cursorX, cursorY; } PrintConsole;
/* A stand-in font with set bits (no real glyphs): the bitmap work is the same. */
static inline PrintConsole *c3dglHostConsole(void) {
    static u8 glyphs[256 * 8];
    static PrintConsole p;
    if (!p.font.gfx) {
        for (int i = 0; i < 256 * 8; i++) glyphs[i] = (u8)(i * 37 + 11);
        p.font.gfx = glyphs;
        p.font.numChars = 256;
    }
    return &p;
}
typedef struct { s16 dx, dy; } circlePosition;
enum {
    KEY_A = 1 << 0, KEY_B = 1 << 1, KEY_SELECT = 1 << 2, KEY_START = 1 << 3, KEY_DRIGHT = 1 << 4, KEY_DLEFT = 1 << 5,
    KEY_DUP = 1 << 6, KEY_DDOWN = 1 << 7, KEY_R = 1 << 8, KEY_L = 1 << 9, KEY_X = 1 << 10, KEY_Y = 1 << 11,
    KEY_ZL = 1 << 14, KEY_ZR = 1 << 15, KEY_TOUCH = 1 << 20, KEY_CSTICK_RIGHT = 1 << 24, KEY_CSTICK_LEFT = 1 << 25,
    KEY_CSTICK_UP = 1 << 26, KEY_CSTICK_DOWN = 1 << 27, KEY_CPAD_RIGHT = 1 << 28, KEY_CPAD_LEFT = 1 << 29,
    KEY_CPAD_UP = 1 << 30, KEY_CPAD_DOWN = 1u << 31,
    KEY_UP = KEY_DUP | KEY_CPAD_UP, KEY_DOWN = KEY_DDOWN | KEY_CPAD_DOWN, KEY_LEFT = KEY_DLEFT | KEY_CPAD_LEFT,
    KEY_RIGHT = KEY_DRIGHT | KEY_CPAD_RIGHT,
};
#define SYSCLOCK_ARM11 268111856LL
#define CUR_PROCESS_HANDLE 0xffff8001
static inline u64 svcGetSystemTick(void) {
    static mach_timebase_info_data_t base;
    if (!base.denom) mach_timebase_info(&base);
    return (u64)((__int128)mach_absolute_time() * base.numer / base.denom * SYSCLOCK_ARM11 / 1000000000);
}
static inline u64 osGetTime(void) { return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000); }
static inline void gfxInitDefault(void) {}
static inline void gfxExit(void) {}
static inline u8 *gfxGetFramebuffer(gfxScreen_t s, gfx3dSide_t d, u16 *w, u16 *h) { (void)s; (void)d; (void)w; (void)h; static u8 fb[400 * 240 * 3]; return fb; }
static inline PrintConsole *consoleInit(gfxScreen_t s, PrintConsole *c) { (void)s; return c ? c : c3dglHostConsole(); }
static inline PrintConsole *consoleGetDefault(void) { return c3dglHostConsole(); }
static inline void consoleClear(void) {}
static inline PrintConsole *consoleSelect(PrintConsole *c) { return c; }
static inline bool aptMainLoop(void) { return true; }
static inline void hidScanInput(void) {}
static inline u32 hidKeysDown(void) { return 0; }
static inline u32 hidKeysHeld(void) { return 0; }
static inline u32 hidKeysUp(void) { return 0; }
static inline void hidCircleRead(circlePosition *p) { p->dx = p->dy = 0; }
static inline void gspWaitForVBlank(void) {}
static inline Result romfsInit(void) { return 0; }
static inline Result romfsExit(void) { return 0; }
static inline Result svcInvalidateProcessDataCache(Handle h, u32 a, u32 s) { (void)h; (void)a; (void)s; return 0; }
