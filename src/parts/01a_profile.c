// Optional CPU profiler (C3DGL_PROFILE): where the time goes inside c3dgl
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// Profiler
//----------------------------------------------------------------------------------
// Off by default, then every macro below is empty. With C3DGL_PROFILE the entry points that matter for a frame's
// CPU time are timed (svcGetSystemTick) and c3dglProfileDump() prints and resets the totals. Scopes nest: the time
// of a scope is exclusive, what ran inside nested scopes (applyState inside a draw call, say) is charged to them.
#ifdef C3DGL_PROFILE
enum {
    PB_ARR_FAST,        // glDrawArrays, direct decode (arrayTriangleFastPath)
    PB_ARR_GENERIC,     // glDrawArrays, per-vertex loop
    PB_EL_CACHE,        // glDrawElements, drawn from the GPU mesh cache
    PB_EL_FAST,         // glDrawElements, direct decode
    PB_EL_GENERIC,      // glDrawElements, per-vertex loop
    PB_IMMEDIATE,       // glBegin ... glEnd (includes the caller's code in between); verts = glVertex calls
    PB_LISTS,           // glCallList(s) itself, the draws inside are charged to their own buckets
    PB_APPLY_STATE,     // applyState: a draw state change, calls = number of batches started
    PB_APPLY_MATRIX,    // applyMatrixOnly: a new batch whose state differs only in the matrices
    PB_FLUSH,           // C3D_DrawArrays of a batch
    PB_FLUSH_CACHE,     // CPU cache flushes before the GPU reads the vertices
    PB_SWAP,            // C3D_FrameEnd
    PB_TEXTURE,         // glTexImage2D, glTexSubImage2D
    PB_BUFFER,          // glBufferData, glBufferSubData
    PB_CLEAR,           // glClear
    PB_PREPARE,         // prepareDraw: building the draw state key and comparing it with the batch's
    PB_DRAWCACHE,       // drawGpuCache: buffer binding and draw of a cached mesh
    PB_PRESENT_SYNC,    // C3D_FrameSync before presenting: waiting for the VBlank
    PB_FRAME_BEGIN,     // ensureFrame: C3D_FrameBegin and C3D_FrameDrawOn (the first draw of a frame pays for it)
    PB_FRAME_SETUP,    // ensureFrame: deferred deletes, GPU cache collection, pixel chunks
    PB_TARGET_SWITCH,  // c3dglSetScreen: relinking and drawing on the other render target
    PB_LINK_TARGET,    // linkTarget inside it
    PB_DRAW_ON,        // C3D_FrameDrawOn inside it
    PB_COUNT
};
static const char *const profNames[PB_COUNT] = {
    "arr-fast", "arr-gen", "el-cache", "el-fast", "el-gen", "imm", "lists", "apply", "apply-mtx", "flush", "cacheflush", "swap",
    "tex", "buffer", "clear", "prepare", "drawcache", "presentsync", "framebegin", "framesetup", "targetswitch", "linktarget", "drawon"
};

// Why the direct-decode paths gave up (fastPathReject), counted
static struct { const char *why; u32 count; } profRejects[24];
static void profReject(const char *why)
{
    for (int i = 0; i < 24; i++)
    {
        if (profRejects[i].why == why || profRejects[i].why == NULL) { profRejects[i].why = why; profRejects[i].count++; return; }
    }
}

typedef struct ProfScope { u64 start, child; struct ProfScope *parent; } ProfScope;
static struct {
    u32 calls[PB_COUNT];
    u32 verts[PB_COUNT];
    u64 ticks[PB_COUNT];
    ProfScope *top;
} prof;
static int profPath;                // Bucket a draw call chose for itself (fast path taken or not)
static ProfScope profImmScope;      // glBegin ... glEnd
static bool profImmActive;

static inline void profEnter(ProfScope *s)
{
    s->start = svcGetSystemTick();
    s->child = 0;
    s->parent = prof.top;
    prof.top = s;
}

static inline void profLeave(ProfScope *s, int bucket, u32 verts)
{
    u64 d = svcGetSystemTick() - s->start;
    prof.ticks[bucket] += d - s->child;
    prof.calls[bucket]++;
    prof.verts[bucket] += verts;
    prof.top = s->parent;
    if (s->parent) s->parent->child += d;
}

#define PROF_ENTER()            ProfScope profScope; profEnter(&profScope)
#define PROF_LEAVE(b, v)        profLeave(&profScope, (b), (u32)(v))
#define PROF_ENTER2()           ProfScope profScope2; profEnter(&profScope2)    // A second scope in the same block
#define PROF_LEAVE2(b, v)       profLeave(&profScope2, (b), (u32)(v))
#define PROF_PATH(b)            (profPath = (b))
#define PROF_COUNT(b)           (prof.verts[b]++)
#define PROF_REJECT(why)        profReject(why)
#define PROF_IMM_ENTER()        do { profEnter(&profImmScope); profImmActive = true; } while (0)
#define PROF_IMM_LEAVE()        do { if (profImmActive) { profImmActive = false; profLeave(&profImmScope, PB_IMMEDIATE, 0); } } while (0)

// Averages over `frames` frames: exclusive ms per frame, calls per frame, vertices per frame
void c3dglProfileDump(int frames)
{
    if (frames < 1) frames = 1;
    double total = 0;
    char line[512];
    int n = 0;
    for (int b = 0; b < PB_COUNT; b++)
    {
        if (prof.calls[b] == 0) continue;
        double ms = (double)prof.ticks[b]*1000.0/SYSCLOCK_ARM11/frames;
        total += ms;
        n += snprintf(line + n, sizeof(line) - n, " %s=%.2f/%u/%u", profNames[b], ms, (unsigned)(prof.calls[b]/frames), (unsigned)(prof.verts[b]/frames));
        if (n >= (int)sizeof(line) - 40) break;
    }
    printf("[c3d] ms/calls/verts per frame:%s | sum=%.2f\n", line, total);
    n = 0;
    for (int i = 0; i < 24 && profRejects[i].why; i++)
    {
        n += snprintf(line + n, sizeof(line) - n, " %s=%u", profRejects[i].why, (unsigned)(profRejects[i].count/frames));
        profRejects[i].count = 0;
    }
    if (n) printf("[c3d-rej] per frame:%s\n", line);
    memset(prof.calls, 0, sizeof(prof.calls));
    memset(prof.verts, 0, sizeof(prof.verts));
    memset(prof.ticks, 0, sizeof(prof.ticks));
}
#else
#define PROF_ENTER()            ((void)0)
#define PROF_LEAVE(b, v)        ((void)0)
#define PROF_ENTER2()           ((void)0)
#define PROF_LEAVE2(b, v)       ((void)0)
#define PROF_PATH(b)            ((void)0)
#define PROF_COUNT(b)           ((void)0)
#define PROF_REJECT(why)        ((void)0)
#define PROF_IMM_ENTER()        ((void)0)
#define PROF_IMM_LEAVE()        ((void)0)
void c3dglProfileDump(int frames) { (void)frames; }
#endif
