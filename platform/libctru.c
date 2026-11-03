/* libctru backend. No PICA encoding or Citro3D calls belong here. */
#include <3ds.h>
#include <stddef.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdatomic.h>
#include <string.h>

/* Command lists split around queued copies (glCopyPixels) need room beyond clear, list and transfer. */
static gxCmdEntry_s entries[32];
static gxCmdQueue_s queue;
static aptHookCookie hook;
static LightEvent completed;
static bool acquired, pending, swap_on_finish;
static u64 started, gpu_ticks;
static u8 *last_output;
static atomic_uint generation;

void *c3dglPlatformAllocate(size_t size, size_t alignment) {
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    return memalign(alignment, size);
}
void c3dglPlatformFree(void *pointer) { free(pointer); }
void *c3dglPlatformLinearAllocate(size_t size) { return linearAlloc(size); }
void c3dglPlatformLinearFree(void *pointer) { linearFree(pointer); }
void *c3dglPlatformVramAllocate(size_t size) { return vramAlloc(size); }
void c3dglPlatformVramFree(void *pointer) { vramFree(pointer); }
u32 c3dglPlatformPhysical(const void *pointer) { return osConvertVirtToPhys(pointer); }
u64 c3dglPlatformTick(void) { return svcGetSystemTick(); }
int c3dglPlatformFlush(const void *pointer, u32 size) {
    return GSPGPU_FlushDataCache(pointer, size);
}

static void finish(gxCmdQueue_s *unused) {
    (void)unused;
    gpu_ticks = svcGetSystemTick() - started;
    if (swap_on_finish) gfxScreenSwapBuffers(GFX_TOP, false);
    /* Waiters must see the swap and timing before they reuse any memory. */
    LightEvent_Signal(&completed);
}

u64 c3dglPlatformWait(void) {
    if (pending) {
        LightEvent_Wait(&completed);
        gxCmdQueueWait(&queue, -1);
        pending = false;
    }
    gxCmdQueueStop(&queue);
    gxCmdQueueClear(&queue);
    return gpu_ticks;
}

static void on_apt(APT_HookType type, void *unused) {
    (void)unused;
    if (type == APTHOOK_ONSUSPEND) c3dglPlatformWait();
    if (type == APTHOOK_ONRESTORE) atomic_fetch_add(&generation, 1);
}

int c3dglPlatformAcquire(void) {
    if (acquired || gfxGetScreenFormat(GFX_TOP) != GSP_BGR8_OES) return -1;
    u16 width, height;
    if (!gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &width, &height) || width != 240 || height != 400)
        return -1;
    queue = (gxCmdQueue_s){ .entries = entries, .maxEntries = 32, .callback = finish };
    LightEvent_Init(&completed, RESET_STICKY);
    GX_BindQueue(&queue);
    atomic_store(&generation, 1);
    pending = false;
    last_output = NULL;
    gpu_ticks = 0;
    acquired = true;
    aptHook(&hook, on_apt, NULL);
    return 0;
}

void c3dglPlatformRelease(void) {
    if (!acquired) return;
    c3dglPlatformWait();
    aptUnhook(&hook);
    GX_BindQueue(NULL);
    acquired = false;
}
u32 c3dglPlatformGeneration(void) { return atomic_load(&generation); }
void c3dglPlatformVblank(void) { gspWaitForVBlank(); }

int c3dglPlatformClear(void *color, void *depth, u32 value, u32 bytes) {
    return GX_MemoryFill(color, value, (u32 *)((u8 *)color + bytes), GX_FILL_TRIGGER | GX_FILL_32BIT_DEPTH,
        depth, 0, (u32 *)((u8 *)depth + bytes), GX_FILL_TRIGGER | GX_FILL_32BIT_DEPTH);
}

int c3dglPlatformClearMasked(void *color, void *depth, u32 value, u32 depth_value, u32 bytes, u32 mask) {
    void *c = (mask & 0x4000) ? color : NULL;
    void *d = (mask & 0x0100) ? depth : NULL;
    /* libctru's GX queue counts PSC0 and ignores PSC1. A depth-only fill must
     * therefore use slot 0, or the queue never reaches its completion callback. */
    if (!c) { c = d; value = depth_value; d = NULL; }
    return GX_MemoryFill(c, value, c ? (u32 *)((u8 *)c + bytes) : NULL, c ? GX_FILL_TRIGGER | GX_FILL_32BIT_DEPTH : 0,
        d, depth_value, d ? (u32 *)((u8 *)d + bytes) : NULL, d ? GX_FILL_TRIGGER | GX_FILL_32BIT_DEPTH : 0);
}

int c3dglPlatformSubmit(void *commands, u32 bytes, void *color) {
    /* Command list and transfer after what is queued (a clear, split lists and copies). */
    if (queue.numEntries + 2 > queue.maxEntries) return -1;
    Result result = GX_ProcessCommandList(commands, bytes, GX_CMDLIST_FLUSH);
    if (R_FAILED(result)) return result;
    swap_on_finish = color != NULL;
    if (color) {
        last_output = gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL);
        result = GX_DisplayTransfer(color, GX_BUFFER_DIM(240,400),
            (u32 *)last_output, GX_BUFFER_DIM(240,400),
            GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8));
        if (R_FAILED(result)) { gxCmdQueueClear(&queue); return result; }
    }
    LightEvent_Clear(&completed);
    started = svcGetSystemTick();
    pending = true;
    gxCmdQueueRun(&queue);
    return 0;
}

/* Queue the command list so far and a raw copy (GX texture copy) of `size` bytes in rows of `row_bytes`
 * from `src` to `dst`, leaving `gap` bytes after each output row. Both run after the work queued before;
 * nothing waits. The frame continues with a new command list. */
int c3dglPlatformSplitCopy(void *commands, u32 bytes, void *src, void *dst, u32 row_bytes, u32 gap, u32 size) {
    if (!acquired) return -2;
    if (queue.numEntries + 4 > queue.maxEntries) return -1;
    Result result = 0;
    if (bytes) result = GX_ProcessCommandList(commands, bytes, GX_CMDLIST_FLUSH);
    if (R_SUCCEEDED(result))
        result = GX_TextureCopy((u32 *)src, GX_BUFFER_DIM(row_bytes >> 4, 0), (u32 *)dst,
            GX_BUFFER_DIM(row_bytes >> 4, gap >> 4), size, GX_TRANSFER_RAW_COPY(1));
    if (R_FAILED(result)) return result;
    if (!pending) {
        LightEvent_Clear(&completed);
        swap_on_finish = false;
        started = svcGetSystemTick();
        pending = true;
        gxCmdQueueRun(&queue);
    }
    return 0;
}

/* Copy window columns [line0, line0 + lines) of the tiled RGBA8 color buffer to linear RGBA8 memory (column x:
 * 240 pixels from y = 0 up, bytes A, B, G, R) and wait for it. The GPU must be idle. */
int c3dglPlatformReadColor(void *color, u32 line0, u32 lines, void *out) {
    if (!acquired || !color || !out || !lines) return -2;
    c3dglPlatformWait();
    size_t size = (size_t)lines * 240 * 4;
    GSPGPU_FlushDataCache(out, size);
    swap_on_finish = false;
    Result result = GX_DisplayTransfer((u32 *)((u8 *)color + (size_t)line0 * 240 * 4), GX_BUFFER_DIM(240, lines),
        (u32 *)out, GX_BUFFER_DIM(240, lines),
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    if (R_FAILED(result)) { gxCmdQueueClear(&queue); return result; }
    LightEvent_Clear(&completed);
    started = svcGetSystemTick();
    pending = true;
    gxCmdQueueRun(&queue);
    c3dglPlatformWait();
    GSPGPU_InvalidateDataCache(out, size);
    return 0;
}

/* The reverse for the depth/stencil buffer: linear RGBA8 words to the tiled buffer. The GPU must be idle. */
int c3dglPlatformWriteLines(void *target, u32 line0, u32 lines, void *in) {
    if (!acquired || !target || !in || !lines) return -2;
    c3dglPlatformWait();
    GSPGPU_FlushDataCache(in, (size_t)lines * 240 * 4);
    swap_on_finish = false;
    Result result = GX_DisplayTransfer((u32 *)in, GX_BUFFER_DIM(240, lines),
        (u32 *)((u8 *)target + (size_t)line0 * 240 * 4), GX_BUFFER_DIM(240, lines),
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
    if (R_FAILED(result)) { gxCmdQueueClear(&queue); return result; }
    LightEvent_Clear(&completed);
    started = svcGetSystemTick();
    pending = true;
    gxCmdQueueRun(&queue);
    c3dglPlatformWait();
    return 0;
}

int c3dglPlatformReadback(u8 *pixels, u32 capacity) {
    if (!acquired || !pixels || capacity < 400*240*3 || !last_output) return -2;
    c3dglPlatformWait();
    Result result = svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE, (u32)last_output, 400*240*3);
    if (R_FAILED(result)) return result;
    memcpy(pixels, last_output, 400*240*3);
    return 0;
}
