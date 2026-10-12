/* libctru backend. No PICA encoding or Citro3D calls belong here. */
#include <3ds.h>
#include <stddef.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdatomic.h>
#include <string.h>

static gxCmdEntry_s entries[4];
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
    queue = (gxCmdQueue_s){ .entries = entries, .maxEntries = 4, .callback = finish };
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

int c3dglPlatformSubmit(void *commands, u32 bytes, void *color) {
    /* Clear (optional), command list, transfer: at most three queue entries. */
    if (queue.numEntries > 1) return -1;
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

int c3dglPlatformReadback(u8 *pixels, u32 capacity) {
    if (!acquired || !pixels || capacity < 400*240*3 || !last_output) return -2;
    c3dglPlatformWait();
    Result result = svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE, (u32)last_output, 400*240*3);
    if (R_FAILED(result)) return result;
    memcpy(pixels, last_output, 400*240*3);
    return 0;
}
