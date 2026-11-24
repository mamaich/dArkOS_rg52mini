/*
 * npusr_gles.c - NPU UPSCALE for GLES programs (RetroArch "gl" on KMS/GBM, SDL2 ports ...), as an LD_PRELOAD
 * library: no change to the emulator. The Vulkan rotate layer's NPU SR (npusr.inc.c) carried over to EGL/GLES.
 *
 *   LD_PRELOAD=libnpusr_gles.so
 *   DARKOS_NPU_SR=x3:320x240     the model's scale and the native frame size; the program must show that frame
 *                                enlarged by the scale, nearest (RetroArch: integer scale, no shader/overlay/smoothing)
 *   DARKOS_NPU_SR=x3:auto        the native size from the viewport the program draws the frame with (a viewport
 *                                that is S times an even size): PS1 256/320/368 wide, Saturn 320/352 ...; a model
 *                                for each size met (DARKOS_NPU_SR_MODEL with %d for S, W, H), loaded when first met
 *   DARKOS_NPU_SR_MODEL=...      the .rknn model; {S}, {W}, {H} in it (or printf %d, %d, %d) are the scale and size
 *                                (default /usr/share/darkos/npusr/espcn_x%d_%dx%d.rknn)
 *   DARKOS_NPU_SR_LIB=...        librknnrt.so (default: found by dlopen)
 *   DARKOS_NPU_SR_STATS=1        counters on stderr every 5 s
 *   DARKOS_NPU_SR_DUMP=file.ppm  (tests) the window after the upscale, frame DARKOS_NPU_SR_DUMP_AT (default 30)
 *
 * How: eglSwapBuffers is taken over. Before the real swap, in a GLES 3.1 context of its own made current on the
 * program's window surface (so the program's GL state is never touched):
 *   1. two blits take one pixel of every SxS block of the native frame from the back buffer (the block's centre,
 *      and its right neighbour - a block that is not one colour is a menu or text drawn over the frame);
 *   2. a draw into the NPU's input memory (dma-buf imported as an EGLImage, RGBA8 = the 4 int8 luma channels of
 *      a 2x2 cell of the space-to-depth model) and a compute pass counting the blocks not one colour; a fence;
 *   3. a worker thread waits for that fence and runs the NPU (three slots a size, as in the Vulkan layer: the
 *      NPU's / the latest finished / being written; the program never waits for the NPU);
 *   4. the latest finished NPU output (dma-buf as an RGBA8 texture over its NC1HWC2 layout) and that slot's
 *      colour are drawn over the native frame's area of the back buffer - unless that slot counted more than 1%
 *      of blocks not one colour (a menu: the frame is shown as the program drew it).
 * The real swap then presents it as before (RetroArch's libgo2: RGA turns it onto the portrait panel).
 * Written by the AI-SR session (2026-11-24).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>
#include <GLES2/gl2ext.h>
#include "rknn_api.h"

#ifndef EGL_LINUX_DMA_BUF_EXT
#define EGL_LINUX_DMA_BUF_EXT 0x3270
#define EGL_LINUX_DRM_FOURCC_EXT 0x3271
#define EGL_DMA_BUF_PLANE0_FD_EXT 0x3272
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT 0x3273
#define EGL_DMA_BUF_PLANE0_PITCH_EXT 0x3274
#endif
#define FOURCC_ABGR8888 0x34324241u   /* 'AB24': bytes R,G,B,A */

#define EXPORT __attribute__((visibility("default")))
#define SLOTS 3
#define LOG(...) do { fprintf(stderr, "darkos_npusr_gles: " __VA_ARGS__); fputc('\n', stderr); } while (0)

/* ------------------------------------------------------------------ the program's EGL/GL, found at run time */
typedef void (*fn)(void);
static EGLBoolean (*real_swap)(EGLDisplay, EGLSurface);
static void (*real_viewport)(GLint, GLint, GLsizei, GLsizei);

#define EGLF(X) \
    X(EGLContext, eglGetCurrentContext, (void)) \
    X(EGLDisplay, eglGetCurrentDisplay, (void)) \
    X(EGLSurface, eglGetCurrentSurface, (EGLint)) \
    X(EGLBoolean, eglQuerySurface, (EGLDisplay, EGLSurface, EGLint, EGLint *)) \
    X(EGLBoolean, eglQueryContext, (EGLDisplay, EGLContext, EGLint, EGLint *)) \
    X(EGLBoolean, eglChooseConfig, (EGLDisplay, const EGLint *, EGLConfig *, EGLint, EGLint *)) \
    X(EGLContext, eglCreateContext, (EGLDisplay, EGLConfig, EGLContext, const EGLint *)) \
    X(EGLBoolean, eglDestroyContext, (EGLDisplay, EGLContext)) \
    X(EGLBoolean, eglMakeCurrent, (EGLDisplay, EGLSurface, EGLSurface, EGLContext)) \
    X(const char *, eglQueryString, (EGLDisplay, EGLint)) \
    X(EGLint, eglGetError, (void)) \
    X(EGLBoolean, eglBindAPI, (EGLenum)) \
    X(EGLenum, eglQueryAPI, (void)) \
    X(fn, eglGetProcAddress, (const char *))
#define GLF(X) \
    X(const GLubyte *, glGetString, (GLenum)) \
    X(void, glGetIntegerv, (GLenum, GLint *)) \
    X(GLenum, glGetError, (void)) \
    X(void, glGenTextures, (GLsizei, GLuint *)) \
    X(void, glBindTexture, (GLenum, GLuint)) \
    X(void, glTexParameteri, (GLenum, GLenum, GLint)) \
    X(void, glTexStorage2D, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
    X(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
    X(void, glGenFramebuffers, (GLsizei, GLuint *)) \
    X(void, glBindFramebuffer, (GLenum, GLuint)) \
    X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum)) \
    X(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
    X(void, glReadBuffer, (GLenum)) \
    X(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
    X(GLuint, glCreateShader, (GLenum)) \
    X(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
    X(void, glCompileShader, (GLuint)) \
    X(void, glGetShaderiv, (GLuint, GLenum, GLint *)) \
    X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(GLuint, glCreateProgram, (void)) \
    X(void, glAttachShader, (GLuint, GLuint)) \
    X(void, glLinkProgram, (GLuint)) \
    X(void, glGetProgramiv, (GLuint, GLenum, GLint *)) \
    X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
    X(void, glUseProgram, (GLuint)) \
    X(GLint, glGetUniformLocation, (GLuint, const GLchar *)) \
    X(void, glUniform1i, (GLint, GLint)) \
    X(void, glUniform2i, (GLint, GLint, GLint)) \
    X(void, glActiveTexture, (GLenum)) \
    X(void, glDrawArrays, (GLenum, GLint, GLsizei)) \
    X(void, glGenVertexArrays, (GLsizei, GLuint *)) \
    X(void, glBindVertexArray, (GLuint)) \
    X(void, glGenBuffers, (GLsizei, GLuint *)) \
    X(void, glBindBuffer, (GLenum, GLuint)) \
    X(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum)) \
    X(void, glBufferSubData, (GLenum, GLintptr, GLsizeiptr, const void *)) \
    X(void, glBindBufferBase, (GLenum, GLuint, GLuint)) \
    X(void *, glMapBufferRange, (GLenum, GLintptr, GLsizeiptr, GLbitfield)) \
    X(GLboolean, glUnmapBuffer, (GLenum)) \
    X(void, glDispatchCompute, (GLuint, GLuint, GLuint)) \
    X(void, glMemoryBarrier, (GLbitfield)) \
    X(void, glDisable, (GLenum)) \
    X(void, glEnable, (GLenum)) \
    X(void, glScissor, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, glClear, (GLbitfield)) \
    X(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) \
    X(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
    X(void, glInvalidateSubFramebuffer, (GLenum, GLsizei, const GLenum *, GLint, GLint, GLsizei, GLsizei)) \
    X(void, glFlush, (void)) \
    X(void, glFinish, (void))

#define DECL(r, n, a) static r (*p_##n) a;
EGLF(DECL)
GLF(DECL)
#undef DECL
static PFNEGLCREATEIMAGEKHRPROC p_eglCreateImageKHR;
static PFNEGLDESTROYIMAGEKHRPROC p_eglDestroyImageKHR;
static PFNEGLCREATESYNCKHRPROC p_eglCreateSyncKHR;
static PFNEGLCLIENTWAITSYNCKHRPROC p_eglClientWaitSyncKHR;
static PFNEGLDESTROYSYNCKHRPROC p_eglDestroySyncKHR;
static PFNEGLWAITSYNCKHRPROC p_eglWaitSyncKHR;   /* optional: the GPU waits, not the CPU */
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC p_glEGLImageTargetTexture2DOES;

static int load_syms(void)
{
    int ok = 1;
#define LD(r, n, a) if (!(*(void **)&p_##n = dlsym(RTLD_DEFAULT, #n))) { LOG("no %s", #n); ok = 0; }
    EGLF(LD)
#undef LD
    if (!ok) return 0;
    /* ours is exported too (glViewport for programs that ask): the driver's own */
    *(void **)&p_eglGetProcAddress = dlsym(RTLD_NEXT, "eglGetProcAddress");
    if (!p_eglGetProcAddress) { LOG("no eglGetProcAddress"); return 0; }
#define LG(r, n, a) if (!(*(void **)&p_##n = (void *)p_eglGetProcAddress(#n)) && !(*(void **)&p_##n = dlsym(RTLD_DEFAULT, #n))) { LOG("no %s", #n); ok = 0; }
    GLF(LG)
#undef LG
    if (real_viewport) *(void **)&p_glViewport = (void *)real_viewport;
    *(void **)&p_eglCreateImageKHR = (void *)p_eglGetProcAddress("eglCreateImageKHR");
    *(void **)&p_eglDestroyImageKHR = (void *)p_eglGetProcAddress("eglDestroyImageKHR");
    *(void **)&p_eglCreateSyncKHR = (void *)p_eglGetProcAddress("eglCreateSyncKHR");
    *(void **)&p_eglClientWaitSyncKHR = (void *)p_eglGetProcAddress("eglClientWaitSyncKHR");
    *(void **)&p_eglDestroySyncKHR = (void *)p_eglGetProcAddress("eglDestroySyncKHR");
    *(void **)&p_eglWaitSyncKHR = (void *)p_eglGetProcAddress("eglWaitSyncKHR");
    *(void **)&p_glEGLImageTargetTexture2DOES = (void *)p_eglGetProcAddress("glEGLImageTargetTexture2DOES");
    if (!p_eglCreateImageKHR || !p_eglDestroyImageKHR || !p_eglCreateSyncKHR || !p_eglClientWaitSyncKHR || !p_eglDestroySyncKHR || !p_glEGLImageTargetTexture2DOES) {
        LOG("no EGLImage / fence sync entry points");
        ok = 0;
    }
    return ok;
}

/* ------------------------------------------------------------------ configuration */
static struct {
    int on, S, W, H, autosize, stats, dump_at, exp, async;   /* async: DARKOS_NPU_SR_COMPOSE=async */   /* exp (tests): 1 no invalidate, 2 no compose, 4 no count, 8 table compose, 16 no NPU jobs */
    char model[512], lib[512], dump[512];
} cfg;

static void read_cfg(void)
{
    const char *e = getenv("DARKOS_NPU_SR");
    if (!e || !*e || *e == '0') return;
    int S = 0, W = 0, H = 0, n = 0;
    if (sscanf(e, "x%d:%n", &S, &n) != 1 || n == 0 || S < 2 || S > 4) goto bad;
    if (!strcmp(e + n, "auto")) cfg.autosize = 1;
    else if (sscanf(e + n, "%dx%d", &W, &H) != 2 || W < 16 || H < 16 || W > 1024 || H > 1024 || (W & 1) || (H & 1)) goto bad;
    cfg.S = S; cfg.W = W; cfg.H = H;
    const char *m = getenv("DARKOS_NPU_SR_MODEL");
    snprintf(cfg.model, sizeof cfg.model, "%s", (m && *m) ? m : "/usr/share/darkos/npusr/espcn_x%d_%dx%d.rknn");
    if (cfg.autosize && !strchr(cfg.model, '%') && !strstr(cfg.model, "{W}")) { LOG("x%d:auto needs DARKOS_NPU_SR_MODEL with {W} and {H}; off", S); return; }
    const char *l = getenv("DARKOS_NPU_SR_LIB");
    snprintf(cfg.lib, sizeof cfg.lib, "%s", (l && *l) ? l : "librknnrt.so");
    const char *st = getenv("DARKOS_NPU_SR_STATS");
    cfg.stats = st && *st && *st != '0';
    const char *d = getenv("DARKOS_NPU_SR_DUMP");
    if (d && *d) snprintf(cfg.dump, sizeof cfg.dump, "%s", d);
    const char *da = getenv("DARKOS_NPU_SR_DUMP_AT");
    cfg.dump_at = (da && *da) ? atoi(da) : 30;
    const char *cm = getenv("DARKOS_NPU_SR_COMPOSE");
    cfg.async = cm && !strcmp(cm, "async");
    const char *x = getenv("DARKOS_NPU_SR_EXP");
    cfg.exp = (x && *x) ? atoi(x) : 0;
    cfg.on = 1;
    if (cfg.autosize) LOG("on: x%d, the size from the viewport, models %s", S, cfg.model);
    else LOG("on: x%d, %dx%d, model %s", S, W, H, cfg.model);
    return;
bad:
    LOG("DARKOS_NPU_SR=\"%s\" not understood (expected e.g. x3:320x240 or x3:auto), off", e);
}

/* ------------------------------------------------------------------ the NPU, one engine a native size */
typedef struct {
    int (*init)(rknn_context *, void *, uint32_t, uint32_t, rknn_init_extend *);
    int (*query)(rknn_context, rknn_query_cmd, void *, uint32_t);
    rknn_tensor_mem *(*create_mem)(rknn_context, uint32_t);
    int (*set_io_mem)(rknn_context, rknn_tensor_mem *, rknn_tensor_attr *);
    int (*run)(rknn_context, rknn_run_extend *);
    int (*destroy_mem)(rknn_context, rknn_tensor_mem *);
    int (*destroy)(rknn_context);
    rknn_tensor_mem *(*create_mem2)(rknn_context, uint64_t, uint64_t);                       /* optional */
    rknn_tensor_mem *(*create_mem_from_fd)(rknn_context, int32_t, void *, uint32_t, int32_t); /* optional */
} Rknn;

/* The NPU's tensors: DARKOS_NPU_SR_ALLOC = npu (rknn_create_mem, cacheable, the default) | npu-nc (non-cacheable) |
 * heap (a dma-buf of /dev/dma_heap/system - or DARKOS_NPU_SR_HEAP - given to the NPU by fd: Mali then imports an
 * ordinary dma-buf, as camera frames, not a GEM object of rknpu). Neither the CPU touches them. */
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
struct heap_alloc { uint64_t len; uint32_t fd; uint32_t fd_flags; uint64_t heap_flags; };
#define HEAP_IOCTL_ALLOC _IOWR('H', 0x0, struct heap_alloc)
static int alloc_mode;          /* 0 npu, 1 npu-nc, 2 heap */
static char heap_name[64] = "system";

typedef struct {
    rknn_tensor_mem *min, *mout;
    EGLImageKHR iin, iout;
    GLuint tin, tout, fbin;     /* NPU input as a render target, NPU output as a texture */
    GLuint ta, fba;             /* the native frame (one pixel a block), bottom-up as GL reads the window */
    GLuint cnt;                 /* SSBO: blocks not one colour */
    EGLSyncKHR fence;
    unsigned count;             /* read back once the slot is finished */
    GLuint trgb, fbrgb;         /* async compose: the finished picture OW x OH, and its FBO in our context */
    GLuint wfb;                 /* its FBO in the worker's context (FBOs are not shared) */
    EGLSyncKHR cfence;          /* the worker's compose of this slot (waited for before the picture is used / the slot reused) */
} Slot;

#define MAXE 6
typedef struct {
    int W, H, ok;               /* ok: 1 ready, -1 failed (no model, wrong tensors: not tried again) */
    rknn_context rc;
    rknn_tensor_attr nin, nout;
    int GW, GH, B, OW, OH, C1, in_ws, out_ws, over;
    Slot slot[SLOTS];
    GLuint tb, fbb;             /* the right neighbour of each block's centre (used only by the count) */
    GLuint prog_prep, prog_cnt, prog_comp, lut, msk;
    GLint u_comp_org;
    int busy, ready;            /* under G.m */
    unsigned long n_comp, n_menu;
} Eng;

static struct {
    int state;                  /* 0 = not tried, 1 = running, -1 = off */
    EGLDisplay dpy;
    EGLContext ctx;
    EGLContext wctx;            /* async compose: the worker's context, sharing objects with ctx, no surface */
    int ac;                     /* async compose on: the worker composes right after the NPU, the swap only copies */
    GLuint wvao;
    void *lib;
    Rknn r;
    Eng e[MAXE];
    int ne;
    GLuint vao;
    pthread_t th;
    int th_on, quit, atexit_set, in_run, sig_set;
    pthread_mutex_t m;
    pthread_cond_t cv;
    Eng *pend; int pslot;       /* the one job waiting for the worker */
    int vx, vy, vw, vh;         /* the last viewport that looked like the frame (S times an even size) */
    int bx, by, bw, bh;         /* the last large viewport that is not S times a size (a frame scaled by S..S+1) */
    int nw, nh;                 /* the last small viewport (a native frame drawn at its own size, into an FBO) */
    unsigned vseq, bseq, seq;   /* which of vw / bw was set last */
    unsigned long n_frames, n_prep, n_npu, n_skip, n_fail, n_geom;
    double swap_ms; unsigned long n_swap;   /* the real swap (SDL_GL_SwapWindow / eglSwapBuffers): how long it blocks */
    double npu_ms, t_last, hook_ms, ph[6];   /* CPU ms: make ours current, prep, count read, compose, flush, back */
    unsigned long last_frames, last_npu;
    Eng *last_e;
} G = { .m = PTHREAD_MUTEX_INITIALIZER, .cv = PTHREAD_COND_INITIALIZER };

static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

static GLuint fbo_for(GLuint t);

static void *worker(void *arg)
{
    (void)arg;
    /* termination signals go to the program's threads, never to this one while it is inside rknn_run */
    sigset_t ss;
    sigemptyset(&ss); sigaddset(&ss, SIGTERM); sigaddset(&ss, SIGINT); sigaddset(&ss, SIGHUP); sigaddset(&ss, SIGQUIT);
    pthread_sigmask(SIG_BLOCK, &ss, NULL);
    /* async compose: this thread's own context (no surface), objects shared with ours; its own VAO and FBOs */
    if (G.ac) {
        if (!p_eglMakeCurrent(G.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, G.wctx)) {
            LOG("worker context: eglMakeCurrent 0x%x - compose at the swap", p_eglGetError());
            __atomic_store_n(&G.ac, 0, __ATOMIC_SEQ_CST);
        } else {
            p_glGenVertexArrays(1, &G.wvao);
            p_glBindVertexArray(G.wvao);
            p_glDisable(GL_BLEND); p_glDisable(GL_DEPTH_TEST); p_glDisable(GL_STENCIL_TEST); p_glDisable(GL_SCISSOR_TEST);
            p_glDisable(GL_CULL_FACE); p_glDisable(GL_DITHER);
        }
    }
    for (;;) {
        pthread_mutex_lock(&G.m);
        while (!G.pend && !G.quit) pthread_cond_wait(&G.cv, &G.m);
        if (G.quit) { pthread_mutex_unlock(&G.m); break; }
        Eng *e = G.pend;
        int k = G.pslot;
        G.pend = NULL;
        e->busy = k;
        EGLSyncKHR f = e->slot[k].fence;
        e->slot[k].fence = NULL;
        pthread_mutex_unlock(&G.m);
        int ok = 0;
        double dt = 0;
        if (f) {
            EGLint w = p_eglClientWaitSyncKHR(G.dpy, f, 0, 1000000000ull);
            p_eglDestroySyncKHR(G.dpy, f);
            /* in_run before quit is looked at (both seq_cst): a signal handler that has set quit either is seen here,
             * or sees in_run and waits for this job */
            __atomic_store_n(&G.in_run, 1, __ATOMIC_SEQ_CST);
            if (w == EGL_CONDITION_SATISFIED_KHR && !__atomic_load_n(&G.quit, __ATOMIC_SEQ_CST)) {
                rknn_tensor_attr in = e->nin, out = e->nout;
                in.pass_through = 1;
                double t0 = now_ms();
                if (cfg.exp & 16) ok = 1;   /* tests without NPU jobs: the output stays as it is (the GPU's cost, geometry) */
                else if (G.r.set_io_mem(e->rc, e->slot[k].min, &in) >= 0 && G.r.set_io_mem(e->rc, e->slot[k].mout, &out) >= 0 &&
                    G.r.run(e->rc, NULL) >= 0)
                    ok = 1;
                dt = now_ms() - t0;
            }
            __atomic_store_n(&G.in_run, 0, __ATOMIC_SEQ_CST);
        }
        /* async compose: the picture of this slot now, while the program's thread works on its next frame; the swap
         * then only copies it (the compose's ~2.5 ms of GPU no longer sits between the program's frame and the flip) */
        if (ok && __atomic_load_n(&G.ac, __ATOMIC_SEQ_CST) && !(cfg.exp & 2)) {
            Slot *s = &e->slot[k];
            if (s->cfence) { p_eglDestroySyncKHR(G.dpy, s->cfence); s->cfence = NULL; }
            /* (a compute pass, one invocation a cell with imageStore, was tried: Mali 45-49% instead of 24-31% - the
             * stores go around the tile memory; the fragment pass stays) */
            {
                if (!s->wfb) s->wfb = fbo_for(s->trgb);
                p_glBindFramebuffer(GL_FRAMEBUFFER, s->wfb);
                p_glViewport(0, 0, e->OW, e->OH);
                p_glUseProgram(e->prog_comp);
                p_glUniform2i(e->u_comp_org, 0, 0);
                p_glActiveTexture(GL_TEXTURE0); p_glBindTexture(GL_TEXTURE_2D, s->tout);
                p_glActiveTexture(GL_TEXTURE1); p_glBindTexture(GL_TEXTURE_2D, s->ta);
                if (cfg.exp & 8) {
                    p_glActiveTexture(GL_TEXTURE2); p_glBindTexture(GL_TEXTURE_2D, e->lut);
                    p_glActiveTexture(GL_TEXTURE3); p_glBindTexture(GL_TEXTURE_2D, e->msk);
                }
                p_glDrawArrays(GL_TRIANGLES, 0, 3);
            }
            s->cfence = p_eglCreateSyncKHR(G.dpy, EGL_SYNC_FENCE_KHR, NULL);
            p_glFlush();
            if (s->cfence == EGL_NO_SYNC_KHR) { s->cfence = NULL; p_glFinish(); }
        }
        pthread_mutex_lock(&G.m);
        e->busy = -1;
        if (ok) { e->ready = k; G.n_npu++; G.npu_ms += dt; } else G.n_fail++;
        pthread_mutex_unlock(&G.m);
    }
    if (G.ac) p_eglMakeCurrent(G.dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    return NULL;
}

/* (main thread, our context current) the worker's compose of a slot is to be finished before its picture is used
 * or the slot is reused: the GPU waits for it (eglWaitSyncKHR), or the CPU when that is missing */
static void cfence_wait(Slot *s)
{
    if (!s->cfence) return;
    if (!p_eglWaitSyncKHR || p_eglWaitSyncKHR(G.dpy, s->cfence, 0) != EGL_TRUE)
        p_eglClientWaitSyncKHR(G.dpy, s->cfence, 0, 200000000ull);
    p_eglDestroySyncKHR(G.dpy, s->cfence);
    s->cfence = NULL;
}

/* ------------------------------------------------------------------ GL helpers */
static GLuint shader(GLenum type, const char *src)
{
    GLuint s = p_glCreateShader(type);
    p_glShaderSource(s, 1, &src, NULL);
    p_glCompileShader(s);
    GLint ok = 0;
    p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char b[2048]; p_glGetShaderInfoLog(s, sizeof b, NULL, b); LOG("shader: %s", b); return 0; }
    return s;
}

static GLuint program(const char *vs, const char *fs, const char *cs)
{
    GLuint p = p_glCreateProgram();
    if (cs) { GLuint c = shader(GL_COMPUTE_SHADER, cs); if (!c) return 0; p_glAttachShader(p, c); }
    else {
        GLuint v = shader(GL_VERTEX_SHADER, vs), f = shader(GL_FRAGMENT_SHADER, fs);
        if (!v || !f) return 0;
        p_glAttachShader(p, v); p_glAttachShader(p, f);
    }
    p_glLinkProgram(p);
    GLint ok = 0;
    p_glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char b[2048]; p_glGetProgramInfoLog(p, sizeof b, NULL, b); LOG("link: %s", b); return 0; }
    return p;
}

static const char *VS =
    "#version 310 es\n"
    "void main() { vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";

static EGLImageKHR import(int fd, int w, int h, int pitch)
{
    EGLint a[] = { EGL_WIDTH, w, EGL_HEIGHT, h, EGL_LINUX_DRM_FOURCC_EXT, (EGLint)FOURCC_ABGR8888,
                   EGL_DMA_BUF_PLANE0_FD_EXT, fd, EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0, EGL_DMA_BUF_PLANE0_PITCH_EXT, pitch, EGL_NONE };
    EGLImageKHR im = p_eglCreateImageKHR(G.dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, a);
    if (im == EGL_NO_IMAGE_KHR) LOG("EGLImage %dx%d pitch %d from fd %d: error 0x%x", w, h, pitch, fd, p_eglGetError());
    return im;
}

static void tex_params(GLint filter)
{
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    p_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static GLuint tex_from_image(EGLImageKHR im, GLint filter)
{
    GLuint t;
    p_glGenTextures(1, &t);
    p_glBindTexture(GL_TEXTURE_2D, t);
    p_glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, im);
    tex_params(filter);
    return t;
}

static GLuint tex_new(GLenum fmt, int w, int h, GLint filter)
{
    GLuint t;
    p_glGenTextures(1, &t);
    p_glBindTexture(GL_TEXTURE_2D, t);
    p_glTexStorage2D(GL_TEXTURE_2D, 1, fmt, w, h);
    tex_params(filter);
    return t;
}

static GLuint fbo_for(GLuint t)
{
    GLuint f;
    p_glGenFramebuffers(1, &f);
    p_glBindFramebuffer(GL_FRAMEBUFFER, f);
    p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
    GLenum st = p_glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (st != GL_FRAMEBUFFER_COMPLETE) { LOG("framebuffer incomplete 0x%x", st); return 0; }
    return f;
}

#define CHK(x) do { if (!(x)) { LOG("%s failed (line %d)", #x, __LINE__); return 0; } } while (0)

/* The end of the NPU work, before the program takes EGL down (eglTerminate) or exits (atexit): the job in flight is
 * waited for, then the tensors and models are freed while nothing runs. A process that ended with the worker inside
 * rknn_run left the NPU stuck ("RKNPU: job timeout ... soft reset" at 3 of 6 RetroArch exits), and after three of
 * those rknpu 0.9.8 oopsed in rknpu_job_subcore_commit and the console hung (2026-11-24, dArkOS kernel 5.10.226).
 * egl: our EGL objects can still be destroyed (eglTerminate; at exit the display may be gone already).
 * After eglTerminate everything is made again on the next frame (state 0); after exit, off. */
static void npu_stop(int egl)
{
    if (G.th_on) {
        pthread_mutex_lock(&G.m);
        __atomic_store_n(&G.quit, 1, __ATOMIC_RELEASE);
        pthread_cond_signal(&G.cv);
        pthread_mutex_unlock(&G.m);
        pthread_join(G.th, NULL);
        G.th_on = 0;
    }
    for (int i = 0; i < G.ne; i++) {
        Eng *E = &G.e[i];
        for (int k = 0; k < SLOTS; k++) {
            Slot *s = &E->slot[k];
            if (egl) {
                if (s->fence) p_eglDestroySyncKHR(G.dpy, s->fence);
                if (s->cfence) p_eglDestroySyncKHR(G.dpy, s->cfence);
                if (s->iin) p_eglDestroyImageKHR(G.dpy, s->iin);
                if (s->iout) p_eglDestroyImageKHR(G.dpy, s->iout);
            }
            if (E->rc) {
                if (s->min) G.r.destroy_mem(E->rc, s->min);
                if (s->mout) G.r.destroy_mem(E->rc, s->mout);
            }
        }
        if (E->rc) G.r.destroy(E->rc);
    }
    if (egl && G.wctx != EGL_NO_CONTEXT) p_eglDestroyContext(G.dpy, G.wctx);
    if (egl && G.ctx != EGL_NO_CONTEXT) p_eglDestroyContext(G.dpy, G.ctx);
    memset(G.e, 0, sizeof G.e);
    G.ne = 0; G.pend = NULL; G.last_e = NULL; G.ctx = EGL_NO_CONTEXT; G.vao = 0;
    G.wctx = EGL_NO_CONTEXT; G.wvao = 0; G.ac = 0;
    G.state = egl ? 0 : -1;
}

static void at_exit(void)
{
    if (G.th_on || G.ne) { npu_stop(0); LOG("exit: NPU work stopped"); }
}

/* A program without a handler of its own for SIGTERM/SIGINT/SIGHUP/SIGQUIT (standalone emulators stopped by a
 * hotkey daemon or systemd) would die with the NPU job in flight: no atexit, no eglTerminate. Ours waits for that
 * job (at most 1 s, async-signal-safe: atomics and nanosleep), then the default action as before. A handler the
 * program sets later replaces ours (it then exits its own way: eglTerminate or atexit). */
static const int term_sigs[] = { SIGTERM, SIGINT, SIGHUP, SIGQUIT };
static void on_term(int sig)
{
    __atomic_store_n(&G.quit, 1, __ATOMIC_SEQ_CST);
    for (int i = 0; i < 200 && __atomic_load_n(&G.in_run, __ATOMIC_SEQ_CST); i++) {
        struct timespec t = { 0, 5000000 };
        nanosleep(&t, NULL);
    }
    static const char msg[] = "darkos_npusr_gles: signal: NPU work stopped\n";
    ssize_t wr = write(2, msg, sizeof msg - 1);   /* async-signal-safe, unlike LOG */
    (void)wr;
    signal(sig, SIG_DFL);
    raise(sig);
}

static void hook_signals(void)
{
    if (G.sig_set) return;
    G.sig_set = 1;
    for (unsigned i = 0; i < sizeof term_sigs / sizeof term_sigs[0]; i++) {
        struct sigaction old, sa;
        if (sigaction(term_sigs[i], NULL, &old) != 0 || (old.sa_flags & SA_SIGINFO) || old.sa_handler != SIG_DFL) continue;
        memset(&sa, 0, sizeof sa);
        sa.sa_handler = on_term;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESETHAND;
        sigaction(term_sigs[i], &sa, NULL);
    }
}

/* once: librknnrt, the worker, the VAO. Our context is current. */
static int setup_common(void)
{
    G.lib = dlopen(cfg.lib, RTLD_NOW | RTLD_LOCAL);
    if (!G.lib) { LOG("dlopen %s: %s", cfg.lib, dlerror()); return 0; }
#define SYM(f, n) CHK((*(void **)&G.r.f = dlsym(G.lib, n)) != NULL)
    SYM(init, "rknn_init"); SYM(query, "rknn_query"); SYM(create_mem, "rknn_create_mem");
    SYM(set_io_mem, "rknn_set_io_mem"); SYM(run, "rknn_run");
    SYM(destroy_mem, "rknn_destroy_mem"); SYM(destroy, "rknn_destroy");
    *(void **)&G.r.create_mem2 = dlsym(G.lib, "rknn_create_mem2");
    *(void **)&G.r.create_mem_from_fd = dlsym(G.lib, "rknn_create_mem_from_fd");
    const char *am = getenv("DARKOS_NPU_SR_ALLOC"), *hn = getenv("DARKOS_NPU_SR_HEAP");
    alloc_mode = !am || !*am || !strcmp(am, "npu") ? 0 : !strcmp(am, "npu-nc") ? 1 : !strcmp(am, "heap") ? 2 : 0;
    if (hn && *hn) snprintf(heap_name, sizeof heap_name, "%s", hn);
    if ((alloc_mode == 1 && !G.r.create_mem2) || (alloc_mode == 2 && !G.r.create_mem_from_fd)) {
        LOG("DARKOS_NPU_SR_ALLOC=%s: not in this librknnrt, using rknn_create_mem", am); alloc_mode = 0;
    }
    LOG("NPU tensors: %s%s", alloc_mode == 2 ? "dma_heap " : alloc_mode == 1 ? "rknn_create_mem2 non-cacheable" : "rknn_create_mem",
        alloc_mode == 2 ? heap_name : "");
#undef SYM
    p_glGenVertexArrays(1, &G.vao);
    p_glBindVertexArray(G.vao);
    p_glDisable(GL_BLEND); p_glDisable(GL_DEPTH_TEST); p_glDisable(GL_STENCIL_TEST); p_glDisable(GL_SCISSOR_TEST);
    p_glDisable(GL_CULL_FACE); p_glDisable(GL_DITHER);
    G.quit = 0;
    CHK(pthread_create(&G.th, NULL, worker, NULL) == 0);
    G.th_on = 1;
    /* after librknnrt is loaded: atexit handlers run in reverse order, so this one runs before its destructors */
    if (!G.atexit_set) { atexit(at_exit); G.atexit_set = 1; }
    hook_signals();
    G.t_last = now_ms();
    LOG("GL %s | %s", (const char *)p_glGetString(GL_VERSION), (const char *)p_glGetString(GL_RENDERER));
    return 1;
}

/* an engine for the native size W x H: the model, its memory, the slots, the programs. Our context is current. */
static rknn_tensor_mem *npu_mem(rknn_context rc, uint32_t size)
{
    if (alloc_mode == 1) return G.r.create_mem2(rc, size, RKNN_FLAG_MEMORY_NON_CACHEABLE);
    if (alloc_mode == 2) {
        char dev[96];
        snprintf(dev, sizeof dev, "/dev/dma_heap/%s", heap_name);
        int h = open(dev, O_RDONLY | O_CLOEXEC);
        if (h < 0) { LOG("open %s failed", dev); return NULL; }
        struct heap_alloc a = { .len = size, .fd_flags = O_RDWR | O_CLOEXEC };
        int r = ioctl(h, HEAP_IOCTL_ALLOC, &a);
        close(h);
        if (r < 0) { LOG("%s: alloc %u failed", dev, size); return NULL; }
        void *va = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, (int)a.fd, 0);
        if (va == MAP_FAILED) va = NULL;
        rknn_tensor_mem *m = G.r.create_mem_from_fd(rc, (int32_t)a.fd, va, size, 0);
        if (!m) LOG("rknn_create_mem_from_fd(%s, %u) failed", dev, size);
        return m;
    }
    return G.r.create_mem(rc, size);
}

static void model_path(char *out, size_t n, int S, int W, int H)
{
    if (!strchr(cfg.model, '{')) { snprintf(out, n, cfg.model, S, W, H); return; }
    size_t o = 0;
    for (const char *p = cfg.model; *p && o + 8 < n; ) {
        int v = -1;
        if (!strncmp(p, "{S}", 3)) v = S; else if (!strncmp(p, "{W}", 3)) v = W; else if (!strncmp(p, "{H}", 3)) v = H;
        if (v >= 0) { o += snprintf(out + o, n - o, "%d", v); p += 3; } else out[o++] = *p++;
    }
    out[o] = 0;
}

static int eng_setup(Eng *E)
{
    char path[600];
    model_path(path, sizeof path, cfg.S, E->W, E->H);
    FILE *f = fopen(path, "rb");
    if (!f) { LOG("%dx%d: no model %s", E->W, E->H, path); return 0; }
    fseek(f, 0, SEEK_END); long msz = ftell(f); fseek(f, 0, SEEK_SET);
    void *model = malloc(msz);
    size_t got = model ? fread(model, 1, msz, f) : 0;
    fclose(f);
    if (got != (size_t)msz) { free(model); LOG("cannot read model %s", path); return 0; }
    int rr = G.r.init(&E->rc, model, (uint32_t)msz, 0, NULL);
    free(model);
    if (rr < 0) { LOG("rknn_init(%s) = %d", path, rr); return 0; }
    CHK(G.r.query(E->rc, RKNN_QUERY_NATIVE_INPUT_ATTR, &E->nin, sizeof E->nin) >= 0);
    CHK(G.r.query(E->rc, RKNN_QUERY_NATIVE_OUTPUT_ATTR, &E->nout, sizeof E->nout) >= 0);
    /* space-to-depth only: input NHWC 1x(H/2)x(W/2)x4 int8; output NC1HWC2 1xC1xGHxGWx16 int8, B = 2S */
    if (!(E->nin.fmt == RKNN_TENSOR_NHWC && E->nin.n_dims == 4 && E->nin.dims[3] == 4 &&
          (int)E->nin.dims[1] == E->H / 2 && (int)E->nin.dims[2] == E->W / 2)) {
        LOG("%s: input %ux%ux%ux%u fmt %d is not the space-to-depth one for %dx%d", path, E->nin.dims[0], E->nin.dims[1],
            E->nin.dims[2], E->nin.dims[3], E->nin.fmt, E->W, E->H);
        return 0;
    }
    E->GW = E->W / 2; E->GH = E->H / 2; E->B = 2 * cfg.S; E->OW = E->GW * E->B; E->OH = E->GH * E->B;
    E->C1 = (int)E->nout.dims[1];
    if (E->nout.fmt != RKNN_TENSOR_NC1HWC2 || E->nout.n_dims != 5 || E->nout.dims[4] != 16 || (int)E->nout.dims[2] != E->GH ||
        (int)E->nout.dims[3] != E->GW || E->C1 * 16 < E->B * E->B || E->B > 8) {
        LOG("%s: output %ux%ux%ux%ux%u fmt %d does not match", path, E->nout.dims[0], E->nout.dims[1], E->nout.dims[2],
            E->nout.dims[3], E->nout.dims[4], E->nout.fmt);
        return 0;
    }
    E->in_ws = E->nin.w_stride ? (int)E->nin.w_stride : E->GW;
    E->out_ws = E->nout.w_stride ? (int)E->nout.w_stride : E->GW;
    E->over = E->W * E->H / 100;
    uint32_t isz = (E->nin.size_with_stride + 4095) & ~4095u, osz = (E->nout.size_with_stride + 4095) & ~4095u;
    if ((uint32_t)(E->in_ws * 4 * E->GH) > isz || (uint32_t)(E->out_ws * 16 * E->GH * E->C1) > osz) {
        LOG("tensor sizes: in %u (need %d), out %u (need %d)", isz, E->in_ws * 4 * E->GH, osz, E->out_ws * 16 * E->GH * E->C1);
        return 0;
    }
    for (int k = 0; k < SLOTS; k++) {
        Slot *s = &E->slot[k];
        s->min = npu_mem(E->rc, isz);
        s->mout = npu_mem(E->rc, osz);
        CHK(s->min && s->mout);
        s->iin = import(s->min->fd, E->in_ws, E->GH, E->in_ws * 4);
        s->iout = import(s->mout->fd, E->out_ws * 4, E->GH * E->C1, E->out_ws * 16);
        CHK(s->iin != EGL_NO_IMAGE_KHR && s->iout != EGL_NO_IMAGE_KHR);
        s->tin = tex_from_image(s->iin, GL_NEAREST);
        CHK((s->fbin = fbo_for(s->tin)) != 0);
        s->tout = tex_from_image(s->iout, GL_NEAREST);
        s->ta = tex_new(GL_RGBA8, E->W, E->H, GL_LINEAR);
        CHK((s->fba = fbo_for(s->ta)) != 0);
        if (G.ac) {
            s->trgb = tex_new(GL_RGBA8, E->OW, E->OH, GL_NEAREST);
            CHK((s->fbrgb = fbo_for(s->trgb)) != 0);
        }
        p_glGenBuffers(1, &s->cnt);
        p_glBindBuffer(GL_SHADER_STORAGE_BUFFER, s->cnt);
        unsigned z[4] = { 0 };
        p_glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof z, z, GL_DYNAMIC_READ);
    }
    E->tb = tex_new(GL_RGBA8, E->W, E->H, GL_NEAREST);
    CHK((E->fbb = fbo_for(E->tb)) != 0);
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);

    char src[4096];
    /* prep: one texel of the NPU input = the int8 luma of a 2x2 cell (channel dy*2+dx), as R,G,B,A */
    snprintf(src, sizeof src,
        "#version 310 es\n"
        "precision highp float; precision highp int;\n"
        "uniform highp sampler2D ta;\n"
        "out vec4 o;\n"
        "float q(ivec2 p) {\n"       /* p: native pixel, top-down; ta is bottom-up */
        "  vec3 c = texelFetch(ta, ivec2(p.x, %d - p.y), 0).rgb;\n"
        "  int v = int(round(clamp(dot(c, vec3(0.299, 0.587, 0.114)), 0.0, 1.0) * 255.0)) - 128;\n"
        "  return float(v & 255) / 255.0; }\n"
        "void main() { ivec2 p = ivec2(gl_FragCoord.xy) * 2;\n"
        "  o = vec4(q(p), q(p + ivec2(1, 0)), q(p + ivec2(0, 1)), q(p + ivec2(1, 1))); }\n", E->H - 1);
    CHK((E->prog_prep = program(VS, src, NULL)) != 0);
    /* count: blocks whose centre and right neighbour differ */
    snprintf(src, sizeof src,
        "#version 310 es\n"
        "layout(local_size_x = 16, local_size_y = 16) in;\n"
        "uniform highp sampler2D ta, tb;\n"
        "layout(std430, binding = 0) buffer C { uint n; } cnt;\n"
        "shared uint s;\n"
        "void main() {\n"
        "  if (gl_LocalInvocationIndex == 0u) s = 0u;\n"
        "  barrier();\n"
        "  ivec2 p = ivec2(gl_GlobalInvocationID.xy);\n"
        "  if (p.x < %d && p.y < %d) {\n"
        "    vec3 d = abs(texelFetch(ta, p, 0).rgb - texelFetch(tb, p, 0).rgb);\n"
        "    if (any(greaterThan(d, vec3(1.5 / 255.0)))) atomicAdd(s, 1u); }\n"
        "  barrier();\n"
        "  if (gl_LocalInvocationIndex == 0u && s > 0u) atomicAdd(cnt.n, s); }\n", E->W, E->H);
    CHK((E->prog_cnt = program(NULL, NULL, src)) != 0);
    if (!(cfg.exp & 8)) {
        /* compose: the upscaled luma (NC1HWC2 as RGBA8 texels: x = gx*4 + (c&15)/4, y = (c/16)*GH + gy, byte c&3)
         * and the native colour (bilinear) */
        snprintf(src, sizeof src,
            "#version 310 es\n"
            "precision highp float; precision highp int;\n"
            "uniform highp sampler2D tout;\n"
            "uniform mediump sampler2D ta;\n"
            "uniform ivec2 org;\n"            /* the area's lower-left corner in the window (GL coordinates) */
            "out vec4 o;\n"
            "void main() {\n"
            "  ivec2 f = ivec2(gl_FragCoord.xy) - org;\n"
            "  ivec2 r = ivec2(f.x, %d - f.y);\n"   /* top-down in the upscaled frame */
            "  ivec2 g = r / %d; ivec2 i = r - g * %d; int c = i.y * %d + i.x;\n"
            "  vec4 t = texelFetch(tout, ivec2(g.x * 4 + ((c & 15) >> 2), (c >> 4) * %d + g.y), 0);\n"
            "  int b = int(t[c & 3] * 255.0 + 0.5);\n"
            "  float Y = clamp((float((b ^ 128) - 128) - (%d.0)) * %.9g, 0.0, 1.0);\n"
            "  mediump vec3 rgb = texture(ta, (vec2(f) + 0.5) / vec2(%d.0, %d.0)).rgb;\n"
            "  mediump float yl = dot(rgb, vec3(0.299, 0.587, 0.114));\n"
            "  mediump float cr = (rgb.r - yl) * 0.713, cb = (rgb.b - yl) * 0.564;\n"
            "  o = vec4(clamp(vec3(Y + 1.403 * cr, Y - 0.714 * cr - 0.344 * cb, Y + 1.773 * cb), 0.0, 1.0), 1.0); }\n",
            E->OH - 1, E->B, E->B, E->B, E->GH, E->nout.zp, E->nout.scale, E->OW, E->OH);
    } else {
        /* lighter: where each pixel of a BxB cell lives in the NC1HWC2 output comes from two BxB tables
         * (the texture unit works beside the ALU), the byte is picked by a dot with a one-hot mask */
        uint16_t lt[8 * 8 * 4];
        uint8_t mt[8 * 8 * 4];
        for (int dy = 0; dy < E->B; dy++)
            for (int dx = 0; dx < E->B; dx++) {
                int c = dy * E->B + dx, k = (dy * E->B + dx) * 4;
                lt[k] = (uint16_t)((c & 15) >> 2); lt[k + 1] = (uint16_t)((c >> 4) * E->GH); lt[k + 2] = lt[k + 3] = 0;
                for (int j = 0; j < 4; j++) mt[k + j] = (j == (c & 3)) ? 255 : 0;
            }
        E->lut = tex_new(GL_RGBA16UI, E->B, E->B, GL_NEAREST);
        p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, E->B, E->B, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, lt);
        E->msk = tex_new(GL_RGBA8, E->B, E->B, GL_NEAREST);
        p_glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, E->B, E->B, GL_RGBA, GL_UNSIGNED_BYTE, mt);
        snprintf(src, sizeof src,
            "#version 310 es\n"
            "precision highp float; precision highp int;\n"
            "uniform highp sampler2D tout;\n"
            "uniform mediump sampler2D ta;\n"
            "uniform highp usampler2D lut;\n"
            "uniform mediump sampler2D msk;\n"
            "uniform ivec2 org;\n"
            "out vec4 o;\n"
            "void main() {\n"
            "  ivec2 f = ivec2(gl_FragCoord.xy) - org;\n"
            "  ivec2 r = ivec2(f.x, %d - f.y);\n"
            "  ivec2 g = r / %d; ivec2 i = r - g * %d;\n"
            "  ivec2 L = ivec2(texelFetch(lut, i, 0).xy);\n"
            "  float v = dot(texelFetch(tout, ivec2(g.x * 4 + L.x, L.y + g.y), 0), texelFetch(msk, i, 0));\n"
            "  float Y = clamp((v * 255.0 - (v >= 0.5 ? 256.0 : 0.0) - (%d.0)) * %.9g, 0.0, 1.0);\n"
            "  mediump vec3 rgb = texture(ta, (vec2(f) + 0.5) / vec2(%d.0, %d.0)).rgb;\n"
            /* Y with the native colour's Cr/Cb, back to RGB = that colour moved by Y minus its own luma
             * (1.403*0.713 and 1.773*0.564 are 1; G follows from the luma weights) */
            "  o = vec4(clamp(rgb + (Y - dot(rgb, vec3(0.299, 0.587, 0.114))), 0.0, 1.0), 1.0); }\n",
            E->OH - 1, E->B, E->B, E->nout.zp, E->nout.scale, E->OW, E->OH);
    }
    CHK((E->prog_comp = program(VS, src, NULL)) != 0);
    E->u_comp_org = p_glGetUniformLocation(E->prog_comp, "org");
    p_glUseProgram(E->prog_prep); p_glUniform1i(p_glGetUniformLocation(E->prog_prep, "ta"), 0);
    p_glUseProgram(E->prog_cnt); p_glUniform1i(p_glGetUniformLocation(E->prog_cnt, "ta"), 0);
    p_glUniform1i(p_glGetUniformLocation(E->prog_cnt, "tb"), 1);
    p_glUseProgram(E->prog_comp); p_glUniform1i(p_glGetUniformLocation(E->prog_comp, "tout"), 0);
    p_glUniform1i(p_glGetUniformLocation(E->prog_comp, "ta"), 1);
    if (cfg.exp & 8) {
        p_glUniform1i(p_glGetUniformLocation(E->prog_comp, "lut"), 2);
        p_glUniform1i(p_glGetUniformLocation(E->prog_comp, "msk"), 3);
    }
    p_glUseProgram(0);
    GLenum er = p_glGetError();
    if (er) { LOG("GL error 0x%x in setup", er); return 0; }
    LOG("%dx%d ready: NPU grid %dx%d, %dx%d px a cell, upscaled %dx%d, in w_stride %d, out w_stride %d x %d groups, zp %d scale %g, %s",
        E->W, E->H, E->GW, E->GH, E->B, E->B, E->OW, E->OH, E->in_ws, E->out_ws, E->C1, E->nout.zp, E->nout.scale, path);
    return 1;
}

static Eng *eng_get(int W, int H)
{
    for (int i = 0; i < G.ne; i++)
        if (G.e[i].W == W && G.e[i].H == H) return G.e[i].ok > 0 ? &G.e[i] : NULL;
    if (G.ne >= MAXE) return NULL;
    Eng *E = &G.e[G.ne++];
    memset(E, 0, sizeof *E);
    E->W = W; E->H = H; E->busy = E->ready = -1;
    E->ok = eng_setup(E) ? 1 : -1;
    return E->ok > 0 ? E : NULL;
}

/* our context, on the program's display and window: its config, ES 3.1, nothing shared */
static int make_ctx(EGLDisplay dpy, EGLContext app)
{
    EGLint id = 0, n = 0;
    EGLConfig c;
    if (!p_eglQueryContext(dpy, app, EGL_CONFIG_ID, &id)) { LOG("no config id"); return 0; }
    EGLint ca[] = { EGL_CONFIG_ID, id, EGL_NONE };
    if (!p_eglChooseConfig(dpy, ca, &c, 1, &n) || n < 1) { LOG("config %d not found", id); return 0; }
    EGLenum api = p_eglQueryAPI();
    p_eglBindAPI(EGL_OPENGL_ES_API);
    EGLint a31[] = { EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 1, EGL_NONE };
    EGLint a3[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    G.ctx = p_eglCreateContext(dpy, c, EGL_NO_CONTEXT, a31);
    if (G.ctx == EGL_NO_CONTEXT) G.ctx = p_eglCreateContext(dpy, c, EGL_NO_CONTEXT, a3);
    if (G.ctx == EGL_NO_CONTEXT) { p_eglBindAPI(api); LOG("eglCreateContext (ES 3.1, config %d): 0x%x", id, p_eglGetError()); return 0; }
    /* DARKOS_NPU_SR_COMPOSE=async: the worker's context, sharing ours, made current without a surface. The swap
     * then only copies the picture (+~6 points of Mali for the copy, but the compose is off the program's critical
     * path: standalone yabasanshiro 51 -> 56 fps); swap (the default): the compose at the swap, less GPU */
    const char *ex = p_eglQueryString(dpy, EGL_EXTENSIONS);
    G.ac = 0; G.wctx = EGL_NO_CONTEXT;
    if (cfg.async && ex && strstr(ex, "EGL_KHR_surfaceless_context")) {
        G.wctx = p_eglCreateContext(dpy, c, G.ctx, a31);
        if (G.wctx == EGL_NO_CONTEXT) G.wctx = p_eglCreateContext(dpy, c, G.ctx, a3);
        G.ac = G.wctx != EGL_NO_CONTEXT;
    }
    p_eglBindAPI(api);
    LOG("compose: %s", G.ac ? "async (in the NPU thread's context, the swap copies)" : "at the swap");
    return 1;
}

static void dump_window(int fw, int fh, const char *path)
{
    unsigned char *px = malloc((size_t)fw * fh * 4);
    if (!px) return;
    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    p_glReadPixels(0, 0, fw, fh, GL_RGBA, GL_UNSIGNED_BYTE, px);
    FILE *f = fopen(path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", fw, fh);
        for (int y = fh - 1; y >= 0; y--)
            for (int x = 0; x < fw; x++) fwrite(px + ((size_t)y * fw + x) * 4, 1, 3, f);
        fclose(f);
        LOG("dumped %dx%d to %s", fw, fh, path);
    }
    free(px);
}

/* ------------------------------------------------------------------ the hooks */
static pthread_once_t once = PTHREAD_ONCE_INIT;
static void init_once(void)
{
    *(void **)&real_swap = dlsym(RTLD_NEXT, "eglSwapBuffers");
    *(void **)&real_viewport = dlsym(RTLD_NEXT, "glViewport");
    read_cfg();
}

/* The frame's geometry: RetroArch draws the frame with glViewport(the frame's rectangle); a viewport that is S times
 * an even size is remembered (the window's own 1280x720 is not: 1280 is not a multiple of 3) */
static void viewport_seen(GLint x, GLint y, GLsizei w, GLsizei h)
{
    int S = cfg.S;
    if (cfg.stats) {   /* (tests) each new viewport size once, the first 12: what geometry the program draws with */
        static int seen[12][2], ns;
        int i = 0;
        while (i < ns && !(seen[i][0] == w && seen[i][1] == h)) i++;
        if (i == ns && ns < 12) { seen[ns][0] = w; seen[ns][1] = h; ns++; LOG("viewport %dx%d at (%d,%d)", w, h, x, y); }
    }
    if (!cfg.on || !S || w <= 0 || h <= 0) return;
    G.seq++;
    if (w % S == 0 && h % S == 0 && !((w / S) & 1) && !((h / S) & 1) &&
        w / S >= 64 && h / S >= 64 && w / S <= 1024 && h / S <= 1024) {
        G.vx = x; G.vy = y; G.vw = w; G.vh = h; G.vseq = G.seq;
    } else if (w >= S * 64 && h >= S * 64) {
        G.bx = x; G.by = y; G.bw = w; G.bh = h; G.bseq = G.seq;
    } else if (w >= 64 && h >= 64 && w <= 1024 && h <= 1024 && !(w & 1) && !(h & 1)) {
        G.nw = w; G.nh = h;
    }
}

/* The native size of a frame the program scaled by S..S+1 into bw x bh (yabasanshiro fits the height: 320x224 ->
 * 1028x720, nearest): the small viewport it drew the frame with, if its shape is that of the area; else the known
 * console sizes of that shape. The S times frame is then drawn centred in the area (same shape, a little smaller). */
static int native_from(int bw, int bh, int *W, int *H)
{
    int S = cfg.S;
    double a = (double)bw / bh;
#define FITS(w, h) (S * (w) <= bw && S * (h) <= bh && bw < (S + 1) * (w) && bh < (S + 1) * (h))
    if (G.nw && FITS(G.nw, G.nh) && fabs((double)G.nw / G.nh - a) < 0.01 * a) { *W = G.nw; *H = G.nh; return 1; }
    static const int ws[] = { 256, 320, 352, 368, 384 }, hs[] = { 224, 240, 256 };
    double best = 0.005;
    int ok = 0;
    for (unsigned i = 0; i < sizeof ws / sizeof *ws; i++)
        for (unsigned j = 0; j < sizeof hs / sizeof *hs; j++) {
            double e = fabs((double)ws[i] / hs[j] - a) / a;
            if (FITS(ws[i], hs[j]) && e < best) { best = e; *W = ws[i]; *H = hs[j]; ok = 1; }
        }
#undef FITS
    return ok;
}

EXPORT void glViewport(GLint x, GLint y, GLsizei w, GLsizei h)
{
    pthread_once(&once, init_once);
    viewport_seen(x, y, w, h);
    if (real_viewport) real_viewport(x, y, w, h);
}

/* programs that take glViewport through eglGetProcAddress (SDL ports) get ours too */
EXPORT fn eglGetProcAddress(const char *name)
{
    pthread_once(&once, init_once);
    static fn (*real_gpa)(const char *);
    if (!real_gpa) *(void **)&real_gpa = dlsym(RTLD_NEXT, "eglGetProcAddress");
    if (name && !strcmp(name, "glViewport") && cfg.on) return (fn)glViewport;
    return real_gpa ? real_gpa(name) : NULL;
}

static void frame(EGLDisplay dpy, EGLSurface surf)
{
    EGLContext app = p_eglGetCurrentContext();
    EGLSurface rd = p_eglGetCurrentSurface(EGL_READ), dr = p_eglGetCurrentSurface(EGL_DRAW);
    if (app == EGL_NO_CONTEXT || dr != surf) return;
    EGLint fw = 0, fh = 0;
    p_eglQuerySurface(dpy, surf, EGL_WIDTH, &fw);
    p_eglQuerySurface(dpy, surf, EGL_HEIGHT, &fh);
    int S = cfg.S, W = 0, H = 0, ox, gy0;
    /* the native frame: the viewport the program drew it with, or (a fixed size) centred. fx, fy, fw2 x fh2: the
     * area the program drew it into - the same as the S times area, or larger (scaled by S..S+1, see native_from) */
    int fx, fy, fw2, fh2;
    if (cfg.autosize || (G.vw == cfg.W * S && G.vh == cfg.H * S)) {
        /* the scaled-by-more area only when it was set after the S times one and has a console shape (a full
         * window viewport such as 1280x720 set after the frame's does not) */
        int nat = cfg.autosize && G.bw && G.bseq > G.vseq && native_from(G.bw, G.bh, &W, &H);
        if (nat) {
            fx = G.bx; fy = G.by; fw2 = G.bw; fh2 = G.bh;
            ox = fx + (fw2 - W * S) / 2; gy0 = fy + (fh2 - H * S) / 2;
        } else if (G.vw) {
            W = G.vw / S; H = G.vh / S; ox = G.vx; gy0 = G.vy;
            fx = ox; fy = gy0; fw2 = W * S; fh2 = H * S;
        } else { G.n_geom++; return; }
    } else {
        W = cfg.W; H = cfg.H; ox = (fw - W * S) / 2; gy0 = (fh - H * S) - (fh - H * S) / 2;
        fx = ox; fy = gy0; fw2 = W * S; fh2 = H * S;
    }
    int aw = W * S, ah = H * S;
    if (fx < 0 || fy < 0 || fx + fw2 > fw || fy + fh2 > fh) { G.n_geom++; return; }
    double t0 = now_ms();
    Eng *E = NULL;
    int ready = -1;
    if (G.state == 0) {
        G.dpy = dpy;
        if (!make_ctx(dpy, app) || !p_eglMakeCurrent(dpy, surf, surf, G.ctx)) { G.state = -1; LOG("off"); goto restore; }
        G.state = setup_common() ? 1 : -1;
        if (G.state < 0) { LOG("NPU SR unavailable, off"); goto restore; }
    } else if (!p_eglMakeCurrent(dpy, surf, surf, G.ctx)) {
        LOG("eglMakeCurrent: 0x%x, off", p_eglGetError()); G.state = -1; goto restore;
    }
    double tp = now_ms(); G.ph[0] += tp - t0;
    E = eng_get(W, H);
    if (!E) { G.n_geom++; goto done; }
    G.last_e = E;
    G.n_frames++;
    /* 1-2: a free slot of this size's engine gets this frame */
    pthread_mutex_lock(&G.m);
    int w = -1;
    ready = E->ready;
    if (!G.pend)
        for (int k = 0; k < SLOTS; k++) if (k != E->busy && k != ready) { w = k; break; }
    pthread_mutex_unlock(&G.m);
    if (w < 0) G.n_skip++;
    else {
        Slot *s = &E->slot[w];
        cfence_wait(s);   /* its last picture (maybe never shown) is finished before its textures are written again */
        /* blits: dst pixel i samples src x0 + (i + 0.5) * fw2 / W - the block's centre (S = 3: its middle pixel;
         * scaled by 3.2: the middle of its 3-4 pixels); tb one to the right (or left when the area touches the
         * window's right edge) - still inside the same block */
        int rx = (fx + fw2 < fw) ? 1 : -1;
        p_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        p_glReadBuffer(GL_BACK);
        p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s->fba);
        p_glBlitFramebuffer(fx, fy, fx + fw2, fy + fh2, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        p_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, E->fbb);
        p_glBlitFramebuffer(fx + rx, fy, fx + rx + fw2, fy + fh2, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        /* the NPU input */
        p_glBindFramebuffer(GL_FRAMEBUFFER, s->fbin);
        p_glViewport(0, 0, E->GW, E->GH);
        p_glUseProgram(E->prog_prep);
        p_glActiveTexture(GL_TEXTURE0);
        p_glBindTexture(GL_TEXTURE_2D, s->ta);
        p_glDrawArrays(GL_TRIANGLES, 0, 3);
        /* the count */
        if (!(cfg.exp & 4)) {
            unsigned z = 0;
            p_glBindBuffer(GL_SHADER_STORAGE_BUFFER, s->cnt);
            p_glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof z, &z);
            p_glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s->cnt);
            p_glUseProgram(E->prog_cnt);
            p_glActiveTexture(GL_TEXTURE1);
            p_glBindTexture(GL_TEXTURE_2D, E->tb);
            p_glDispatchCompute((W + 15) / 16, (H + 15) / 16, 1);
            p_glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
        }
        EGLSyncKHR fence = p_eglCreateSyncKHR(dpy, EGL_SYNC_FENCE_KHR, NULL);
        p_glFlush();
        if (fence == EGL_NO_SYNC_KHR) G.n_fail++;
        else {
            pthread_mutex_lock(&G.m);
            s->fence = fence;
            G.pend = E; G.pslot = w;
            pthread_cond_signal(&G.cv);
            pthread_mutex_unlock(&G.m);
            G.n_prep++;
        }
    }
    { double t1 = now_ms(); G.ph[1] += t1 - tp; tp = t1; }
    /* 4: the latest finished slot over the area, unless it was a menu */
    if (ready >= 0) {
        Slot *s = &E->slot[ready];
        p_glBindBuffer(GL_SHADER_STORAGE_BUFFER, s->cnt);
        unsigned *pc = p_glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, sizeof(unsigned), GL_MAP_READ_BIT);
        s->count = pc ? *pc : 0;
        p_glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
        { double t1 = now_ms(); G.ph[2] += t1 - tp; tp = t1; }
        if ((int)s->count > E->over) E->n_menu++;
        else if (!(cfg.exp & 2)) {
            p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
            /* the area is drawn over entirely: its old contents need not be loaded into the tile memory */
            if (fw2 != aw || fh2 != ah) {
                /* a frame scaled by more than S: the S times picture goes in the middle, the rest of the area black */
                p_glEnable(GL_SCISSOR_TEST);
                p_glScissor(fx, fy, fw2, fh2);
                p_glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                p_glClear(GL_COLOR_BUFFER_BIT);
                p_glDisable(GL_SCISSOR_TEST);
            } else if (!(cfg.exp & 1)) { static const GLenum col = GL_COLOR; p_glInvalidateSubFramebuffer(GL_FRAMEBUFFER, 1, &col, ox, gy0, aw, ah); }
            if (__atomic_load_n(&G.ac, __ATOMIC_SEQ_CST) && s->fbrgb) {
                /* async: the worker's picture, copied (EXP bit 64, tests: not copied - the copy's own cost) */
                if (!(cfg.exp & 64)) {
                    cfence_wait(s);
                    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, s->fbrgb);
                    p_glBlitFramebuffer(0, 0, E->OW, E->OH, ox, gy0, ox + aw, gy0 + ah, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                    p_glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                }
            } else {
                p_glViewport(ox, gy0, aw, ah);
                p_glUseProgram(E->prog_comp);
                p_glUniform2i(E->u_comp_org, ox, gy0);
                p_glActiveTexture(GL_TEXTURE0);
                p_glBindTexture(GL_TEXTURE_2D, s->tout);
                p_glActiveTexture(GL_TEXTURE1);
                p_glBindTexture(GL_TEXTURE_2D, s->ta);
                if (cfg.exp & 8) {
                    p_glActiveTexture(GL_TEXTURE2); p_glBindTexture(GL_TEXTURE_2D, E->lut);
                    p_glActiveTexture(GL_TEXTURE3); p_glBindTexture(GL_TEXTURE_2D, E->msk);
                }
                p_glDrawArrays(GL_TRIANGLES, 0, 3);
            }
            E->n_comp++;
        }
    }
    { double t1 = now_ms(); G.ph[3] += t1 - tp; tp = t1; }
    if (cfg.dump[0] && G.n_frames == (unsigned long)cfg.dump_at) dump_window(fw, fh, cfg.dump);
done:
    p_glFlush();
    { double t1 = now_ms(); G.ph[4] += t1 - tp; }
    G.hook_ms += now_ms() - t0;
restore:
    { double t1 = now_ms(); p_eglMakeCurrent(dpy, dr, rd, app); double t2 = now_ms(); G.ph[5] += t2 - t1; G.hook_ms += t2 - t1; }
    if (cfg.stats && G.state > 0) {
        double t = now_ms();
        if (t - G.t_last >= 5000) {
            double dt = (t - G.t_last) / 1e3;
            unsigned long nf = G.n_frames - G.last_frames, nn = G.n_npu - G.last_npu;
            Eng *L = G.last_e;
            LOG("5.0 s: frames %.1f/s, NPU %.1f/s (avg %.2f ms), hook %.2f ms/frame (CPU), %dx%d at (%d,%d) composed %lu menu %lu, "
                "skipped %lu, failed %lu, other geometry %lu, not one colour %u",
                nf / dt, nn / dt, G.n_npu ? G.npu_ms / G.n_npu : 0.0, G.n_frames ? G.hook_ms / G.n_frames : 0.0,
                L ? L->W : 0, L ? L->H : 0, ox, gy0, L ? L->n_comp : 0ul, L ? L->n_menu : 0ul, G.n_skip, G.n_fail, G.n_geom,
                (L && L->ready >= 0) ? L->slot[L->ready].count : 0u);
            double k = G.n_frames ? 1.0 / G.n_frames : 0;
            LOG("   real swap %.2f ms (avg of %lu)", G.n_swap ? G.swap_ms / G.n_swap : 0.0, G.n_swap);
            LOG("   CPU ms/frame: ours current %.2f, prep %.2f, count read %.2f, compose %.2f, flush %.2f, back %.2f",
                G.ph[0] * k, G.ph[1] * k, G.ph[2] * k, G.ph[3] * k, G.ph[4] * k, G.ph[5] * k);
            G.t_last = t; G.last_frames = G.n_frames; G.last_npu = G.n_npu;
        }
    }
}

/* the program takes EGL down (exit, or a video driver re-init): our work ends first, on a display still alive */
EXPORT EGLBoolean eglTerminate(EGLDisplay dpy)
{
    pthread_once(&once, init_once);
    static EGLBoolean (*real_term)(EGLDisplay);
    if (!real_term) *(void **)&real_term = dlsym(RTLD_NEXT, "eglTerminate");
    if ((G.th_on || G.ne) && dpy == G.dpy) { npu_stop(1); LOG("eglTerminate: NPU work stopped"); }
    return real_term ? real_term(dpy) : EGL_FALSE;
}

static __thread int in_sdl_swap;   /* inside SDL_GL_SwapWindow: the frame is done already */

EXPORT EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface surf)
{
    pthread_once(&once, init_once);
    if (!real_swap) return EGL_FALSE;
    if (cfg.on && G.state >= 0 && !in_sdl_swap) {
        if (G.state == 0 && !load_syms()) G.state = -1;
        if (G.state >= 0) frame(dpy, surf);
    }
    if (!cfg.stats) return real_swap(dpy, surf);
    double t = now_ms();
    EGLBoolean r = real_swap(dpy, surf);
    if (!in_sdl_swap) { G.swap_ms += now_ms() - t; G.n_swap++; }
    return r;
}

/* SDL2 programs (standalone emulators, ports): SDL takes eglSwapBuffers / eglTerminate with dlsym from its own
 * handle of libEGL, past LD_PRELOAD - but the program calls SDL_GL_SwapWindow and SDL_Quit itself. */
EXPORT void SDL_GL_SwapWindow(void *window)
{
    pthread_once(&once, init_once);
    static void (*real)(void *);
    if (!real) *(void **)&real = dlsym(RTLD_NEXT, "SDL_GL_SwapWindow");
    if (!real) return;
    if (cfg.on && G.state >= 0) {
        if (G.state == 0 && !load_syms()) G.state = -1;
        if (G.state >= 0) {
            EGLDisplay d = p_eglGetCurrentDisplay();
            EGLSurface s = p_eglGetCurrentSurface(EGL_DRAW);
            if (d != EGL_NO_DISPLAY && s != EGL_NO_SURFACE) frame(d, s);
        }
    }
    in_sdl_swap = 1;
    double t = now_ms();
    real(window);
    if (cfg.stats) { G.swap_ms += now_ms() - t; G.n_swap++; }
    in_sdl_swap = 0;
}

EXPORT void SDL_Quit(void)
{
    pthread_once(&once, init_once);
    static void (*real)(void);
    if (!real) *(void **)&real = dlsym(RTLD_NEXT, "SDL_Quit");
    if (G.th_on || G.ne) { npu_stop(1); LOG("SDL_Quit: NPU work stopped"); G.state = -1; }
    if (real) real();
}
