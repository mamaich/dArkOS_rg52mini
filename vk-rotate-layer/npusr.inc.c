/*
 * npusr.inc.c - NPU UPSCALE: a frame enlarged by a neural network (ESPCN) on the RK3562's NPU, inside the
 * rotate layer. Included by darkos_rotate_layer.c; it does anything only when asked:
 *
 *   DARKOS_NPU_SR=x3:400x240[:a2][:fit|:fill]
 *                              the model's scale and the emulator's native frame size. The program must show
 *                              that frame enlarged by an integer, nearest, centred in its picture, as
 *                              RetroArch's integer scale has it: by the model's scale, or by aA (the PSP's
 *                              x3 model over PPSSPP's 2x). The upscaled frame goes where the program's was,
 *                              or with fit into the largest rectangle of the frame's shape in the picture,
 *                              with fill over all of it (resampled bilinearly when its size is not the model's)
 *   DARKOS_NPU_SR_MODEL=...    the .rknn model (default /usr/share/darkos/npusr/espcn_x<S>_<W>x<H>.rknn)
 *   DARKOS_NPU_SR_LIB=...      librknnrt.so (default: found by dlopen)
 *   DARKOS_NPU_SR_STATS=1      counters on stderr every 5 s
 *
 * How (first written for ESPCN's "row5" models; the image now has space-to-depth ones, see below): at present a
 * compute pass, in a submission of its own, takes every S-th pixel of the area from the program's picture and
 * writes the NPU's input (5 luma channels, its native NC1HWC2 int8 layout) and the
 * colour into a free slot; a worker thread waits for that submission's fence and runs the NPU; the rotate
 * pass builds each pixel inside the area from the latest finished NPU output and the colour. Three slots
 * (the NPU's / the latest finished / being written); present never waits for the NPU: when the NPU is busy
 * and a frame waits already, no new one is queued. The NPU allocates the memory and Vulkan imports it
 * (dma-buf). Written by the AI-SR session (2026-11-22/23); docs/KNOWN-ISSUES.md, "NPU UPSCALE".
 * dArkOS, 2026-11-25: an area that is not the upscaled frame's size (fit, fill, a TV) is no longer resampled
 * from the NPU's output in each of its pixels (four luma fetches and their unpacking: PSP on a 1080p TV took
 * the Mali to 99%, 54 fps): a decode pass builds the upscaled frame in RGBA once a new NPU output, in the
 * present's command buffer, and the frame's pass samples it once a pixel, bilinearly (now sr_decode.frag and
 * sr_rect.frag, below).
 * The AI-SR session, 2026-11-25: the passes made cheaper, the picture the same (measured pass by pass offscreen on
 * the console, the Mali at 900 MHz). The native frame's colour and luma are drawn by a fragment pass into a colour
 * attachment (sr_nat) and the NPU's input is packed from it by two gathers a cell (sr_nin_s2d.comp), rather than
 * fetched and stored a pixel at a time by a compute pass. The decode is a fragment pass (sr_decode.frag) into a
 * colour attachment: neither image has STORAGE usage now, so the driver compresses them (AFBC) and writes them
 * from the tile buffer. And the frame is drawn as a rectangle of its own (sr_frame.frag where it is its own size,
 * sr_rect.frag sampling the decoded one), the bars around it by the plain rotate shader and the program's bars
 * under a moved frame by sr_ring.frag: no pixel tests the rectangle's bounds, and the fetches take their
 * coordinates straight from the varyings. The layer's passes a frame: 3DS x3 on the panel 4.8 -> 3.9 ms, PSP fit
 * 8.3 -> 5.9 ms, 3DS x3 fitted to a 1080p TV 8.4 -> 6.6 ms.
 */
#include <dlfcn.h>
#include <time.h>
#include <unistd.h>
#include "rknn_api.h"
#include "sr_spv.h"   /* sr_prep_spv[], sr_prep_cell_spv[], sr_nat_vert_spv[], sr_nat_frag_spv[], sr_nin_s2d_spv[], sr_rotate_frag_spv[],
                         sr_decode_frag_spv[], sr_frame_frag_spv[], sr_rect_frag_spv[], sr_ring_frag_spv[] */

#define SR_SLOTS 3
#define SRLOG(...) do { fprintf(stderr, "darkos_npusr: " __VA_ARGS__); fputc('\n', stderr); } while (0)

typedef struct {
    int on, S, W, H, stats;
    int Sd;                 /* the scale is S/Sd (8/3: 480x270 to 1280x720) */
    int over;               /* -1: DARKOS_NPU_SR_OVER not given */
    int A;                  /* the step at which the program shows the native frame (its integer scale) */
    int fit;                /* 0: the upscaled frame where the program's is; 1: fit (the frame's shape); 2: fill */
    char model[512], lib[512];
} SRCfg;
static SRCfg g_sr;
static int g_sr_parsed;

static void sr_cfg(void)
{
    if (g_sr_parsed) return;
    g_sr_parsed = 1;
    const char *e = getenv("DARKOS_NPU_SR");
    if (!e || !*e || *e == '0') return;
    int S = 0, Sd = 1, W = 0, H = 0, n = 0, A = 0, fit = 0, bad = 0;
    const char *t = e;
    if (sscanf(t, "x%d%n", &S, &n) != 1) bad = 1; else t += n;
    if (!bad && *t == '/') { if (sscanf(t, "/%d%n", &Sd, &n) != 1) bad = 1; else t += n; }
    if (!bad) { if (sscanf(t, ":%dx%d%n", &W, &H, &n) != 2) bad = 1; else t += n; }
    if (!bad && (Sd < 1 || Sd > 4 || S <= Sd || S > 4 * Sd || W < 16 || H < 16 || W > 2048 || H > 2048)) bad = 1;
    while (!bad && *t) {   /* the options: ":a2", ":fit", ":fill" */
        int k = 0;
        if (sscanf(t, ":a%d%n", &A, &k) == 1 && k > 0 && A >= 1 && A <= 4) t += k;
        else if (!strncmp(t, ":fill", 5)) { fit = 2; t += 5; }
        else if (!strncmp(t, ":fit", 4)) { fit = 1; t += 4; }
        else bad = 1;
    }
    if (!bad && Sd > 1 && !A) bad = 1;   /* a scale that is not whole: the program's step must be given */
    if (bad) {
        SRLOG("DARKOS_NPU_SR=\"%s\" not understood (expected e.g. x3:400x240 or x3:480x272:a2:fit), off", e);
        return;
    }
    g_sr.S = S; g_sr.Sd = Sd; g_sr.W = W; g_sr.H = H; g_sr.A = A ? A : S; g_sr.fit = fit;
    const char *ov = getenv("DARKOS_NPU_SR_OVER");
    g_sr.over = (ov && *ov) ? atoi(ov) : -1;
    const char *m = getenv("DARKOS_NPU_SR_MODEL");
    if (m && *m) snprintf(g_sr.model, sizeof g_sr.model, "%s", m);
    else if (Sd == 1) snprintf(g_sr.model, sizeof g_sr.model, "/usr/share/darkos/npusr/espcn_x%d_%dx%d.rknn", S, W, H);
    else snprintf(g_sr.model, sizeof g_sr.model, "/usr/share/darkos/npusr/espcn_x%d-%d_%dx%d.rknn", S, Sd, W, H);
    const char *l = getenv("DARKOS_NPU_SR_LIB");
    snprintf(g_sr.lib, sizeof g_sr.lib, "%s", (l && *l) ? l : "librknnrt.so");
    const char *st = getenv("DARKOS_NPU_SR_STATS");
    g_sr.stats = st && *st && *st != '0';
    g_sr.on = 1;
    char sd[16] = "";
    if (Sd > 1) snprintf(sd, sizeof sd, "/%d", Sd);
    SRLOG("on: x%d%s, %dx%d, shown x%d, %s, model %s", S, sd, W, H, g_sr.A,
          fit == 2 ? "fill" : fit ? "fit" : "in place", g_sr.model);
}

typedef struct {
    int (*init)(rknn_context *, void *, uint32_t, uint32_t, rknn_init_extend *);
    int (*query)(rknn_context, rknn_query_cmd, void *, uint32_t);
    rknn_tensor_mem *(*create_mem)(rknn_context, uint32_t);
    int (*destroy_mem)(rknn_context, rknn_tensor_mem *);
    int (*set_io_mem)(rknn_context, rknn_tensor_mem *, rknn_tensor_attr *);
    int (*run)(rknn_context, rknn_run_extend *);
    int (*destroy)(rknn_context);
} RknnApi;

typedef struct {
    rknn_tensor_mem *min, *mout;
    VkBuffer bin, bout;
    VkDeviceMemory gin, gout;
    VkImage cbcr;
    VkDeviceMemory gcbcr;
    VkImageView vcbcr;
    VkFramebuffer fb_cbcr;  /* sr_nat draws into it (the space-to-depth models) */
    VkFence fence;          /* this slot's prep has run (the worker waits for it) */
    int cbcr_init;          /* the image is in GENERAL already */
    VkBuffer bcnt;          /* how many blocks of this slot's frame were not one colour (prep counts them) */
    VkDeviceMemory gcnt;
    volatile uint32_t *mcnt;
    VkImage rgb;            /* the upscaled frame in RGBA (sr_decode.frag), for an area of another size */
    VkDeviceMemory grgb;
    VkImageView vrgb;
    VkFramebuffer fb_rgb;
    unsigned gen, dec_gen;  /* this slot's NPU output (the worker counts them) and the one decoded into rgb */
    unsigned long pno;      /* the present that prepped it: its output is shown only while it is young (SR_MAXAGE) */
} SRSlot;

/* The NPU's picture is shown only for a frame at most this many presents old: when the NPU stops answering (a job
 * failing or hung), the program's own picture comes back instead of the last upscaled one standing still (2026-11-28) */
#define SR_MAXAGE 5

typedef struct SRDev {
    Dev *d;
    void *lib;
    RknnApi r;
    rknn_context ctx;
    int have_ctx;
    rknn_tensor_attr nin, nout;
    int kind;               /* the model's: 0 = ESPCN row5 (5 luma channels, NC1HWC2, full grid), 1 = space-to-depth
                               (4 channels NHWC on a grid of 2x2 cells), 2 = cells (K*K channels NC1HWC2, KxK cells) */
    int K;                  /* native pixels a side of one NPU cell (1 for row5) */
    int B, GW, GH;          /* output pixels a side of one NPU cell; the NPU grid */
    int OW, OH;             /* the upscaled frame: GW*B x GH*B */
    int cnt_host;           /* every slot's count readable by the CPU */
    SRSlot slot[SR_SLOTS];
    VkSampler nearest, linear;
    int nat;                /* the native frame is drawn by a fragment pass (sr_nat, sr_nin_s2d.comp: the space-to-depth
                               models); else a compute pass fetches and stores it (sr_prep.comp, sr_prep_cell.comp) */
    VkImageLayout cbcr_layout;   /* the layout the colour image is sampled in */
    VkRenderPass rp_off;    /* an RGBA8 image written whole by a fragment pass and left for sampling */
    VkDescriptorSetLayout prep_dsl, comp_dsl, tex_dsl, nin_dsl;
    VkPipelineLayout prep_pl, nat_pl, nin_pl, comp_pl;
    VkPipeline prep_pipe, nat_pipe, nin_pipe;
    VkDescriptorPool dpool;
    VkDescriptorSet comp_ds[SR_SLOTS], nin_ds[SR_SLOTS];
    /* the decode pass and its images, made when a swapchain first needs them (0 not yet, 1 made, -1 failed) */
    int rgb_on;
    VkDescriptorSetLayout rgb_dsl;
    VkPipelineLayout rgb_pl;
    VkPipeline dec_pipe;
    VkDescriptorPool rgb_pool;
    VkDescriptorSet rgb_ds[SR_SLOTS];
    unsigned gen_seq;
    pthread_t th;
    int th_started;
    pthread_mutex_t m;
    pthread_cond_t cv;
    int quit, pending, busy, ready;
    /* counters */
    unsigned long n_present, n_prep, n_npu, n_skip, n_fail, n_dec;
    double npu_ms, t_last;
    unsigned long last_present, last_npu;
} SRDev;

/* what depends on the swapchain */
typedef struct SRSwap {
    SRDev *sr;
    int ox, oy;             /* the program's native frame (W*A x H*A) in its picture */
    int dx, dy, dw, dh;     /* where the upscaled frame goes */
    int over;               /* more blocks not one colour than this: the program's picture as it is (0: never) */
    int moved;              /* the upscaled frame is not where the program's is */
    int resample;           /* the area is not the upscaled frame's size: sr_decode.frag + sr_rect.frag */
    int old;                /* and no decode pass to be had: the whole picture by sr_rotate.frag, resampled in each pixel */
    VkRect2D bars[4], ring[4];   /* on the display: around the frame's rectangle; the program's bars under a moved frame */
    uint32_t nbars, nring;
    VkPipelineLayout pl;    /* sr_rotate.frag's (old) */
    VkPipeline pipe;        /* the frame: sr_frame.frag, sr_rect.frag, or the whole picture by sr_rotate.frag (old) */
    VkPipeline bars_pipe, ring_pipe;
    VkDescriptorPool dp;
    VkDescriptorSet prep_ds[MAX_IMAGES][SR_SLOTS];   /* the compute prep's */
    VkDescriptorSet nat_ds[MAX_IMAGES];              /* sr_nat's: the program's picture */
    VkCommandPool pool;
    VkCommandBuffer cb_prep[MAX_IMAGES], cb_pres[MAX_IMAGES];
} SRSwap;

typedef struct { int32_t rot, AW, AH, ox, oy, W, H, S, wstride, zp; float scale; int32_t B, GH, A, dx, dy, dw, dh, OW, OH, moved; } SRPush;

static double sr_now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }

/* ------------------------------------------------------------------------ */

static void *sr_worker(void *arg)
{
    SRDev *sr = arg;
    Dev *d = sr->d;
    for (;;) {
        pthread_mutex_lock(&sr->m);
        while (!sr->quit && sr->pending < 0)
            pthread_cond_wait(&sr->cv, &sr->m);
        if (sr->quit) { pthread_mutex_unlock(&sr->m); break; }
        int k = sr->pending;
        sr->pending = -1;
        sr->busy = k;
        pthread_mutex_unlock(&sr->m);

        int ok = 0;
        double dt = 0;
        /* until the prep has run: the slot stays busy meanwhile, so a present does not take it (and reset its fence)
         * while its prep may still be on the GPU; a second at a time, to see a quit */
        VkResult wr;
        while ((wr = d->WaitForFences(d->device, 1, &sr->slot[k].fence, VK_TRUE, 1000000000ull)) == VK_TIMEOUT &&
               !__atomic_load_n(&sr->quit, __ATOMIC_ACQUIRE))
            SRLOG("prep of slot %d not finished in 1 s, waiting on", k);
        if (wr == VK_SUCCESS) {
            rknn_tensor_attr in = sr->nin, out = sr->nout;
            in.pass_through = 1;
            double t0 = sr_now_ms();
            if (sr->r.set_io_mem(sr->ctx, sr->slot[k].min, &in) >= 0 &&
                sr->r.set_io_mem(sr->ctx, sr->slot[k].mout, &out) >= 0 &&
                sr->r.run(sr->ctx, NULL) >= 0)
                ok = 1;
            dt = sr_now_ms() - t0;
        }
        pthread_mutex_lock(&sr->m);
        sr->busy = -1;
        if (ok) { sr->ready = k; sr->n_npu++; sr->npu_ms += dt; sr->slot[k].gen = ++sr->gen_seq; }
        else sr->n_fail++;
        pthread_mutex_unlock(&sr->m);
    }
    return NULL;
}

/* The workers to stop at exit() before librknnrt's own destructors free a job's buffers under a job still on the
 * NPU (the AI-SR session, 2026-11-24: a program leaving without stopping its NPU thread, a job timed out, and the
 * next submit crashed rknpu 0.9.8 - fixed in the kernel too). A program that never calls vkDestroyDevice would
 * leave this worker in rknn_run; registered with atexit() after librknnrt is loaded, this runs before them. */
#define SR_EXIT_MAX 4
static SRDev *g_sr_exit[SR_EXIT_MAX];
static pthread_mutex_t g_sr_exit_lock = PTHREAD_MUTEX_INITIALIZER;

static void sr_atexit(void)
{
    pthread_mutex_lock(&g_sr_exit_lock);
    for (int k = 0; k < SR_EXIT_MAX; k++) {
        SRDev *sr = g_sr_exit[k];
        if (!sr || !sr->th_started) continue;
        pthread_mutex_lock(&sr->m);
        sr->quit = 1;
        pthread_cond_signal(&sr->cv);
        pthread_mutex_unlock(&sr->m);
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 2;
        if (pthread_timedjoin_np(sr->th, NULL, &ts) == 0) {
            sr->th_started = 0;
            SRLOG("exit: NPU worker stopped before the runtime's destructors");
        }
    }
    pthread_mutex_unlock(&g_sr_exit_lock);
}

static void sr_exit_track(SRDev *sr, int add)
{
    static int registered;
    pthread_mutex_lock(&g_sr_exit_lock);
    for (int k = 0; k < SR_EXIT_MAX; k++) {
        if (add && !g_sr_exit[k]) { g_sr_exit[k] = sr; break; }
        if (!add && g_sr_exit[k] == sr) { g_sr_exit[k] = NULL; break; }
    }
    if (add && !registered) registered = atexit(sr_atexit) == 0;
    pthread_mutex_unlock(&g_sr_exit_lock);
}

static void sr_device_free(Dev *d)
{
    SRDev *sr = d->sr;
    if (!sr) return;
    if (sr->th_started) {
        pthread_mutex_lock(&sr->m);
        sr->quit = 1;
        pthread_cond_signal(&sr->cv);
        pthread_mutex_unlock(&sr->m);
        /* do not wait for ever: an NPU job is bounded by the driver's timeout, but the program's exit matters more */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 2;
        if (pthread_timedjoin_np(sr->th, NULL, &ts) != 0) {
            SRLOG("worker did not stop in 2 s, leaving NPU resources to process exit");
            /* free nothing under a live thread, and keep it on the exit list: exit() still waits for it before
             * librknnrt's destructors (the layer is linked -z nodelete, so its code stays while the thread runs) */
            d->sr = NULL;
            return;
        }
        sr->th_started = 0;
    }
    sr_exit_track(sr, 0);
    VkDevice dv = d->device;
    d->DeviceWaitIdle(dv);
    for (int k = 0; k < SR_SLOTS; k++) {
        SRSlot *s = &sr->slot[k];
        if (s->fence) d->DestroyFence(dv, s->fence, NULL);
        if (s->fb_cbcr) d->DestroyFramebuffer(dv, s->fb_cbcr, NULL);
        if (s->fb_rgb) d->DestroyFramebuffer(dv, s->fb_rgb, NULL);
        if (s->vcbcr) d->DestroyImageView(dv, s->vcbcr, NULL);
        if (s->cbcr) d->DestroyImage(dv, s->cbcr, NULL);
        if (s->gcbcr) d->FreeMemory(dv, s->gcbcr, NULL);
        if (s->bcnt) d->DestroyBuffer(dv, s->bcnt, NULL);
        if (s->gcnt) d->FreeMemory(dv, s->gcnt, NULL);
        if (s->bin) d->DestroyBuffer(dv, s->bin, NULL);
        if (s->bout) d->DestroyBuffer(dv, s->bout, NULL);
        if (s->gin) d->FreeMemory(dv, s->gin, NULL);
        if (s->gout) d->FreeMemory(dv, s->gout, NULL);
        if (sr->have_ctx && s->min) sr->r.destroy_mem(sr->ctx, s->min);
        if (sr->have_ctx && s->mout) sr->r.destroy_mem(sr->ctx, s->mout);
        if (s->vrgb) d->DestroyImageView(dv, s->vrgb, NULL);
        if (s->rgb) d->DestroyImage(dv, s->rgb, NULL);
        if (s->grgb) d->FreeMemory(dv, s->grgb, NULL);
    }
    if (sr->dec_pipe) d->DestroyPipeline(dv, sr->dec_pipe, NULL);
    if (sr->rgb_pl) d->DestroyPipelineLayout(dv, sr->rgb_pl, NULL);
    if (sr->rgb_pool) d->DestroyDescriptorPool(dv, sr->rgb_pool, NULL);
    if (sr->rgb_dsl) d->DestroyDescriptorSetLayout(dv, sr->rgb_dsl, NULL);
    if (sr->prep_pipe) d->DestroyPipeline(dv, sr->prep_pipe, NULL);
    if (sr->nat_pipe) d->DestroyPipeline(dv, sr->nat_pipe, NULL);
    if (sr->nin_pipe) d->DestroyPipeline(dv, sr->nin_pipe, NULL);
    if (sr->prep_pl) d->DestroyPipelineLayout(dv, sr->prep_pl, NULL);
    if (sr->nat_pl) d->DestroyPipelineLayout(dv, sr->nat_pl, NULL);
    if (sr->nin_pl) d->DestroyPipelineLayout(dv, sr->nin_pl, NULL);
    if (sr->comp_pl) d->DestroyPipelineLayout(dv, sr->comp_pl, NULL);
    if (sr->dpool) d->DestroyDescriptorPool(dv, sr->dpool, NULL);
    if (sr->prep_dsl) d->DestroyDescriptorSetLayout(dv, sr->prep_dsl, NULL);
    if (sr->comp_dsl) d->DestroyDescriptorSetLayout(dv, sr->comp_dsl, NULL);
    if (sr->tex_dsl) d->DestroyDescriptorSetLayout(dv, sr->tex_dsl, NULL);
    if (sr->nin_dsl) d->DestroyDescriptorSetLayout(dv, sr->nin_dsl, NULL);
    if (sr->rp_off) d->DestroyRenderPass(dv, sr->rp_off, NULL);
    if (sr->nearest) d->DestroySampler(dv, sr->nearest, NULL);
    if (sr->linear) d->DestroySampler(dv, sr->linear, NULL);
    if (sr->have_ctx) sr->r.destroy(sr->ctx);
    /* librknnrt is not unloaded (dlclose): it may have threads of its own; another dlopen returns the same copy */
    free(sr);
    d->sr = NULL;
}

/* a Vulkan buffer over the NPU's memory (dma-buf fd) */
static int sr_import(Dev *d, int fd, VkDeviceSize size, VkBuffer *buf, VkDeviceMemory *mem)
{
    VkMemoryFdPropertiesKHR fdp = { .sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR };
    if (!d->GetMemoryFdPropertiesKHR ||
        d->GetMemoryFdPropertiesKHR(d->device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &fdp) != VK_SUCCESS) {
        SRLOG("vkGetMemoryFdPropertiesKHR failed for fd %d", fd);
        return 0;
    }
    VkExternalMemoryBufferCreateInfo ebi = { .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT };
    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .pNext = &ebi, .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    if (d->CreateBuffer(d->device, &bci, NULL, buf) != VK_SUCCESS) return 0;
    VkMemoryRequirements mr;
    d->GetBufferMemoryRequirements(d->device, *buf, &mr);
    off_t dsz = lseek(fd, 0, SEEK_END);
    VkImportMemoryFdInfoKHR imp = { .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, .fd = dup(fd) };
    VkMemoryDedicatedAllocateInfo ded = { .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .pNext = &imp, .buffer = *buf };
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &ded,
        .allocationSize = (dsz > 0 && (VkDeviceSize)dsz > mr.size) ? (VkDeviceSize)dsz : mr.size,
        .memoryTypeIndex = find_memory(d, fdp.memoryTypeBits & mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
    if (d->AllocateMemory(d->device, &mai, NULL, mem) != VK_SUCCESS) { SRLOG("import alloc failed (fd %d)", fd); close(imp.fd); return 0; }
    return d->BindBufferMemory(d->device, *buf, *mem, 0) == VK_SUCCESS;
}

#define SRCHK(x) do { if (!(x)) { SRLOG("%s failed (line %d)", #x, __LINE__); goto fail; } } while (0)
#define SRVK(x) do { VkResult _r = (x); if (_r != VK_SUCCESS) { SRLOG("%s = %d", #x, _r); goto fail; } } while (0)

/* A graphics pipeline for one of the layer's passes: a triangle over the viewport, one colour attachment, no
 * blending; the scissor is the viewport, or dynamic (the bars, the ring: several rectangles a pass). */
static VkResult sr_gfx_pipe(Dev *d, VkRenderPass rp, VkPipelineLayout pl, const uint32_t *vs, size_t vsz, const uint32_t *fs, size_t fsz,
                            const VkSpecializationInfo *spec, VkViewport vp, int dyn_scissor, VkPipeline *out)
{
    VkDevice dv = d->device;
    VkShaderModule mv, mf;
    VkShaderModuleCreateInfo smi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = vsz, .pCode = vs };
    VkResult r = d->CreateShaderModule(dv, &smi, NULL, &mv);
    if (r != VK_SUCCESS) return r;
    smi.codeSize = fsz; smi.pCode = fs;
    r = d->CreateShaderModule(dv, &smi, NULL, &mf);
    if (r != VK_SUCCESS) { d->DestroyShaderModule(dv, mv, NULL); return r; }
    VkPipelineShaderStageCreateInfo st[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = mv, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = mf, .pName = "main",
          .pSpecializationInfo = spec } };
    VkPipelineVertexInputStateCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkRect2D sc = { { (int32_t)vp.x, (int32_t)vp.y }, { (uint32_t)vp.width, (uint32_t)vp.height } };
    VkPipelineViewportStateCreateInfo vps = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc };
    VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState cba = { .colorWriteMask = 0xF };
    VkPipelineColorBlendStateCreateInfo cbs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &cba };
    VkDynamicState dyn = VK_DYNAMIC_STATE_SCISSOR;
    VkPipelineDynamicStateCreateInfo dsi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 1, .pDynamicStates = &dyn };
    VkGraphicsPipelineCreateInfo gpi = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = st, .pVertexInputState = &vi, .pInputAssemblyState = &ia,
        .pViewportState = &vps, .pRasterizationState = &rs, .pMultisampleState = &ms,
        .pColorBlendState = &cbs, .pDynamicState = dyn_scissor ? &dsi : NULL, .layout = pl, .renderPass = rp, .subpass = 0 };
    r = d->CreateGraphicsPipelines(dv, VK_NULL_HANDLE, 1, &gpi, NULL, out);
    d->DestroyShaderModule(dv, mv, NULL);
    d->DestroyShaderModule(dv, mf, NULL);
    return r;
}

/* a rectangle of the program's (landscape) picture on the display, turned as rotate.vert turns it */
static VkRect2D sr_map_rect(const Swap *s, int x, int y, int w, int h)
{
    if (s->rot == 0) return (VkRect2D){ { x, y }, { (uint32_t)w, (uint32_t)h } };
    if (s->rot == 270) return (VkRect2D){ { y, (int32_t)s->real_ext.height - x - w }, { (uint32_t)h, (uint32_t)w } };
    return (VkRect2D){ { (int32_t)s->real_ext.width - y - h, x }, { (uint32_t)h, (uint32_t)w } };
}

/* Everything that does not depend on the swapchain: the NPU, the slots, the compute pipeline, the worker thread. Once per device. */
static SRDev *sr_device_get(Dev *d)
{
    if (d->sr) return d->sr->have_ctx ? d->sr : NULL;
    SRDev *sr = calloc(1, sizeof *sr);
    if (!sr) return NULL;
    d->sr = sr;
    sr->d = d;
    sr->pending = sr->busy = sr->ready = -1;
    pthread_mutex_init(&sr->m, NULL);
    pthread_cond_init(&sr->cv, NULL);
    VkDevice dv = d->device;

    sr->lib = dlopen(g_sr.lib, RTLD_NOW | RTLD_LOCAL);
    if (!sr->lib) { SRLOG("dlopen %s: %s", g_sr.lib, dlerror()); goto fail; }
#define SYM(f, n) SRCHK((*(void **)&sr->r.f = dlsym(sr->lib, n)) != NULL)
    SYM(init, "rknn_init"); SYM(query, "rknn_query"); SYM(create_mem, "rknn_create_mem");
    SYM(destroy_mem, "rknn_destroy_mem"); SYM(set_io_mem, "rknn_set_io_mem"); SYM(run, "rknn_run"); SYM(destroy, "rknn_destroy");
#undef SYM
    FILE *f = fopen(g_sr.model, "rb");
    if (!f) { SRLOG("cannot open model %s", g_sr.model); goto fail; }
    fseek(f, 0, SEEK_END); long msz = ftell(f); fseek(f, 0, SEEK_SET);
    void *model = malloc(msz);
    size_t got = model ? fread(model, 1, msz, f) : 0;
    fclose(f);
    if (got != (size_t)msz) { free(model); SRLOG("cannot read model"); goto fail; }
    int rr = sr->r.init(&sr->ctx, model, (uint32_t)msz, 0, NULL);
    free(model);
    if (rr < 0) { SRLOG("rknn_init = %d", rr); goto fail; }
    sr->have_ctx = 1;
    SRCHK(sr->r.query(sr->ctx, RKNN_QUERY_NATIVE_INPUT_ATTR, &sr->nin, sizeof sr->nin) >= 0);
    SRCHK(sr->r.query(sr->ctx, RKNN_QUERY_NATIVE_OUTPUT_ATTR, &sr->nout, sizeof sr->nout) >= 0);
    /* three kinds of model, told apart by the input (the output is NC1HWC2 1xC1xGHxGWx16 int8 in all: B*B channels,
     * channel dy*B+dx of a cell, C1*16 >= B*B):
     *   ESPCN row5:     input NC1HWC2 1x1xHxWx16 int8 (5 channels: the luma shifted by -2..2 rows), B = S;
     *   space-to-depth: input NHWC 1x(H/2)x(W/2)x4 int8 (a 2x2 cell, channel dy*2+dx), B = 2S;
     *   cells:          input NC1HWC2 1x1x(H/K)x(W/K)x16 int8 (a KxK cell, channel dy*K+dx, K*K <= 16), B = S*K
     *                   (S may be a fraction: x8/3 from 3x3 cells gives 8x8 pixels a cell). */
    int nc = sr->nin.fmt == RKNN_TENSOR_NC1HWC2 && sr->nin.n_dims == 5 && sr->nin.dims[1] == 1 && sr->nin.dims[4] == 16;
    int K = 0;
    sr->kind = -1;
    if (nc && (int)sr->nin.dims[2] == g_sr.H && (int)sr->nin.dims[3] == g_sr.W) {
        if (g_sr.Sd == 1 && g_sr.S * g_sr.S <= 16) { sr->kind = 0; K = 1; }
    } else if (nc && sr->nin.dims[2] && sr->nin.dims[3] && g_sr.H % (int)sr->nin.dims[2] == 0 &&
               g_sr.H / (int)sr->nin.dims[2] * (int)sr->nin.dims[3] == g_sr.W) {
        K = g_sr.H / (int)sr->nin.dims[2];
        if (K >= 2 && K * K <= 16) sr->kind = 2;
    } else if (sr->nin.fmt == RKNN_TENSOR_NHWC && sr->nin.n_dims == 4 && sr->nin.dims[3] == 4 && !(g_sr.W & 1) && !(g_sr.H & 1) &&
               (int)sr->nin.dims[1] == g_sr.H / 2 && (int)sr->nin.dims[2] == g_sr.W / 2) {
        sr->kind = 1; K = 2;
    }
    if (sr->kind >= 0 && (g_sr.S * K) % g_sr.Sd) sr->kind = -1;   /* a cell must make whole pixels */
    sr->K = K;
    sr->B = K ? g_sr.S * K / g_sr.Sd : 0;
    sr->GW = K ? g_sr.W / K : 0;
    sr->GH = K ? g_sr.H / K : 0;
    sr->OW = sr->GW * sr->B;
    sr->OH = sr->GH * sr->B;
    if (sr->kind < 0 || sr->nout.fmt != RKNN_TENSOR_NC1HWC2 || sr->nout.n_dims != 5 || sr->nout.dims[4] != 16 ||
        (int)sr->nout.dims[2] != sr->GH || (int)sr->nout.dims[3] != sr->GW || (int)sr->nout.dims[1] * 16 < sr->B * sr->B) {
        SRLOG("model tensors do not match x%d/%d:%dx%d (in %ux%ux%ux%ux%u fmt %d, out %ux%ux%ux%ux%u fmt %d)", g_sr.S, g_sr.Sd,
              g_sr.W, g_sr.H, sr->nin.dims[0], sr->nin.dims[1], sr->nin.dims[2], sr->nin.dims[3], sr->nin.dims[4], sr->nin.fmt,
              sr->nout.dims[0], sr->nout.dims[1], sr->nout.dims[2], sr->nout.dims[3], sr->nout.dims[4], sr->nout.fmt);
        goto fail;
    }
    uint32_t isz = (sr->nin.size_with_stride + 4095) & ~4095u, osz = (sr->nout.size_with_stride + 4095) & ~4095u;

    VkSamplerCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST, .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .maxLod = 0.0f };
    SRVK(d->CreateSampler(dv, &sci, NULL, &sr->nearest));
    sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
    SRVK(d->CreateSampler(dv, &sci, NULL, &sr->linear));

    /* An RGBA8 image written whole by a fragment pass and left for sampling: the native frame's colour and luma
     * (sr_nat) and the upscaled frame (sr_decode.frag). It waits for the passes that still sample the image. */
    sr->nat = sr->kind == 1;
    sr->cbcr_layout = sr->nat ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
    VkAttachmentDescription oat = { .format = VK_FORMAT_R8G8B8A8_UNORM, .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
    VkAttachmentReference oref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription osub = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &oref };
    VkSubpassDependency odep[2] = {
        { VK_SUBPASS_EXTERNAL, 0,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0 },
        { 0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
          VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
          VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0 } };
    VkRenderPassCreateInfo orp = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, .attachmentCount = 1, .pAttachments = &oat,
        .subpassCount = 1, .pSubpasses = &osub, .dependencyCount = 2, .pDependencies = odep };
    SRVK(d->CreateRenderPass(dv, &orp, NULL, &sr->rp_off));

    for (int k = 0; k < SR_SLOTS; k++) {
        SRSlot *s = &sr->slot[k];
        s->min = sr->r.create_mem(sr->ctx, isz);
        s->mout = sr->r.create_mem(sr->ctx, osz);
        SRCHK(s->min && s->mout);   /* not touched by the CPU: the GPU writes all 16 bytes of a pixel, and a dirty CPU cache line could overwrite them */
        SRCHK(sr_import(d, s->min->fd, isz, &s->bin, &s->gin));
        SRCHK(sr_import(d, s->mout->fd, osz, &s->bout, &s->gout));
        VkImageCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = { (uint32_t)g_sr.W, (uint32_t)g_sr.H, 1 }, .mipLevels = 1,
            .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
            /* drawn into by sr_nat, or stored into by the compute prep: with STORAGE usage the driver does not compress it */
            .usage = (sr->nat ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_STORAGE_BIT) | VK_IMAGE_USAGE_SAMPLED_BIT,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
        SRVK(d->CreateImage(dv, &ii, NULL, &s->cbcr));
        VkMemoryRequirements mr;
        d->GetImageMemoryRequirements(dv, s->cbcr, &mr);
        VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
            .memoryTypeIndex = find_memory(d, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
        SRVK(d->AllocateMemory(dv, &mai, NULL, &s->gcbcr));
        SRVK(d->BindImageMemory(dv, s->cbcr, s->gcbcr, 0));
        VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = s->cbcr,
            .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        SRVK(d->CreateImageView(dv, &vci, NULL, &s->vcbcr));
        if (sr->nat) {
            VkFramebufferCreateInfo fbi = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = sr->rp_off,
                .attachmentCount = 1, .pAttachments = &s->vcbcr, .width = (uint32_t)g_sr.W, .height = (uint32_t)g_sr.H, .layers = 1 };
            SRVK(d->CreateFramebuffer(dv, &fbi, NULL, &s->fb_cbcr));
        }
        VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
        SRVK(d->CreateFence(dv, &fi, NULL, &s->fence));
        VkBufferCreateInfo nbi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 16,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
        SRVK(d->CreateBuffer(dv, &nbi, NULL, &s->bcnt));
        d->GetBufferMemoryRequirements(dv, s->bcnt, &mr);
        VkMemoryAllocateInfo nai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
            .memoryTypeIndex = find_memory(d, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
        SRVK(d->AllocateMemory(dv, &nai, NULL, &s->gcnt));
        SRVK(d->BindBufferMemory(dv, s->bcnt, s->gcnt, 0));
        /* read by the CPU at each present (which pass draws the frame) and for the counters on stderr: only from
         * memory it sees coherently; without it every frame takes the checking pass */
        VkPhysicalDeviceMemoryProperties mp;
        d->inst->GetMemProps(d->phys, &mp);
        const VkMemoryPropertyFlags hc = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        void *pm = NULL;
        if ((mp.memoryTypes[nai.memoryTypeIndex].propertyFlags & hc) == hc && d->MapMemory &&
            d->MapMemory(dv, s->gcnt, 0, VK_WHOLE_SIZE, 0, &pm) == VK_SUCCESS)
            s->mcnt = pm;
    }
    sr->cnt_host = 1;
    for (int k = 0; k < SR_SLOTS; k++) if (!sr->slot[k].mcnt) sr->cnt_host = 0;
    if (!sr->cnt_host) SRLOG("the count of blocks not one colour is not readable by the CPU: a menu will be upscaled too");

    VkDescriptorSetLayoutCreateInfo dli = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pushConstantRangeCount = 1 };
    VkShaderModule cs;
    VkComputePipelineCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .pName = "main" } };
    VkResult pr;
    if (sr->nat) {
        /* the native frame by a fragment pass (sr_nat): set0 = the program's picture */
        VkDescriptorSetLayoutBinding tb = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &sr->nearest };
        dli.bindingCount = 1; dli.pBindings = &tb;
        SRVK(d->CreateDescriptorSetLayout(dv, &dli, NULL, &sr->tex_dsl));
        VkPushConstantRange npc = { VK_SHADER_STAGE_VERTEX_BIT, 0, 7 * sizeof(int32_t) };
        pli.pSetLayouts = &sr->tex_dsl; pli.pPushConstantRanges = &npc;
        SRVK(d->CreatePipelineLayout(dv, &pli, NULL, &sr->nat_pl));
        int32_t step = g_sr.A;   /* sr_nat.frag's S: the step the program shows the native frame at */
        VkSpecializationMapEntry sse = { 0, 0, sizeof step };
        VkSpecializationInfo ssi = { 1, &sse, sizeof step, &step };
        VkViewport nvp = { 0, 0, (float)g_sr.W, (float)g_sr.H, 0, 1 };
        SRVK(sr_gfx_pipe(d, sr->rp_off, sr->nat_pl, sr_nat_vert_spv, sizeof(sr_nat_vert_spv), sr_nat_frag_spv, sizeof(sr_nat_frag_spv),
                         &ssi, nvp, 0, &sr->nat_pipe));
        /* the NPU's input from it (sr_nin_s2d.comp): set0 = the slot's native frame, its NPU input, its count of blocks not one colour */
        VkDescriptorSetLayoutBinding nb[3] = {
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &sr->nearest },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL },
            { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL } };
        dli.bindingCount = 3; dli.pBindings = nb;
        SRVK(d->CreateDescriptorSetLayout(dv, &dli, NULL, &sr->nin_dsl));
        VkPushConstantRange cpc = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 5 * 4 };
        pli.pSetLayouts = &sr->nin_dsl; pli.pPushConstantRanges = &cpc;
        SRVK(d->CreatePipelineLayout(dv, &pli, NULL, &sr->nin_pl));
        VkShaderModuleCreateInfo smi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = sizeof(sr_nin_s2d_spv), .pCode = sr_nin_s2d_spv };
        SRVK(d->CreateShaderModule(dv, &smi, NULL, &cs));
        cpi.stage.module = cs; cpi.layout = sr->nin_pl;
        pr = d->CreateComputePipelines(dv, VK_NULL_HANDLE, 1, &cpi, NULL, &sr->nin_pipe);
    } else {
        /* prep: set0 = the program's picture, the slot's NPU input, the slot's colour, the slot's count of blocks not one colour */
        VkDescriptorSetLayoutBinding pb[4] = {
            { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &sr->nearest },
            { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL },
            { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL },
            { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL } };
        dli.bindingCount = 4; dli.pBindings = pb;
        SRVK(d->CreateDescriptorSetLayout(dv, &dli, NULL, &sr->prep_dsl));
        VkPushConstantRange ppc = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 7 * sizeof(int32_t) };
        pli.pSetLayouts = &sr->prep_dsl; pli.pPushConstantRanges = &ppc;
        SRVK(d->CreatePipelineLayout(dv, &pli, NULL, &sr->prep_pl));
        VkShaderModuleCreateInfo smi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = sr->kind == 2 ? sizeof(sr_prep_cell_spv) : sizeof(sr_prep_spv),
            .pCode = sr->kind == 2 ? sr_prep_cell_spv : sr_prep_spv };
        SRVK(d->CreateShaderModule(dv, &smi, NULL, &cs));
        cpi.stage.module = cs; cpi.layout = sr->prep_pl;
        pr = d->CreateComputePipelines(dv, VK_NULL_HANDLE, 1, &cpi, NULL, &sr->prep_pipe);
    }
    d->DestroyShaderModule(dv, cs, NULL);
    SRVK(pr);

    /* the frame's passes (sr_frame.frag, sr_decode.frag; sr_rotate.frag's set1): set0 = the slot's NPU output and colour */
    VkDescriptorSetLayoutBinding cb[2] = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &sr->linear } };
    dli.bindingCount = 2; dli.pBindings = cb;
    SRVK(d->CreateDescriptorSetLayout(dv, &dli, NULL, &sr->comp_dsl));
    VkPushConstantRange rpc = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(int32_t) };   /* rotate.vert's rot */
    pli.pSetLayouts = &sr->comp_dsl; pli.pPushConstantRanges = &rpc;
    SRVK(d->CreatePipelineLayout(dv, &pli, NULL, &sr->comp_pl));
    VkDescriptorPoolSize ps[2] = { { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * SR_SLOTS }, { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * SR_SLOTS } };
    VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 2 * SR_SLOTS, .poolSizeCount = 2, .pPoolSizes = ps };
    SRVK(d->CreateDescriptorPool(dv, &dpi, NULL, &sr->dpool));
    for (int k = 0; k < SR_SLOTS; k++) {
        VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = sr->dpool, .descriptorSetCount = 1, .pSetLayouts = &sr->comp_dsl };
        SRVK(d->AllocateDescriptorSets(dv, &dai, &sr->comp_ds[k]));
        VkDescriptorBufferInfo bi = { sr->slot[k].bout, 0, VK_WHOLE_SIZE };
        VkDescriptorImageInfo ci = { sr->linear, sr->slot[k].vcbcr, sr->cbcr_layout };
        VkWriteDescriptorSet w[2] = {
            { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = sr->comp_ds[k], .dstBinding = 0, .descriptorCount = 1,
              .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi },
            { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = sr->comp_ds[k], .dstBinding = 1, .descriptorCount = 1,
              .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &ci } };
        d->UpdateDescriptorSets(dv, 2, w, 0, NULL);
        if (sr->nat) {
            dai.pSetLayouts = &sr->nin_dsl;
            SRVK(d->AllocateDescriptorSets(dv, &dai, &sr->nin_ds[k]));
            VkDescriptorImageInfo ni = { sr->nearest, sr->slot[k].vcbcr, sr->cbcr_layout };
            VkDescriptorBufferInfo ib = { sr->slot[k].bin, 0, VK_WHOLE_SIZE }, nc = { sr->slot[k].bcnt, 0, VK_WHOLE_SIZE };
            VkWriteDescriptorSet nw[3] = {
                { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = sr->nin_ds[k], .dstBinding = 0, .descriptorCount = 1,
                  .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &ni },
                { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = sr->nin_ds[k], .dstBinding = 1, .descriptorCount = 1,
                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &ib },
                { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = sr->nin_ds[k], .dstBinding = 2, .descriptorCount = 1,
                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &nc } };
            d->UpdateDescriptorSets(dv, 3, nw, 0, NULL);
        }
    }
    SRCHK(pthread_create(&sr->th, NULL, sr_worker, sr) == 0);
    sr->th_started = 1;
    sr_exit_track(sr, 1);   /* librknnrt is loaded by now: its destructors come after this atexit() */
    sr->t_last = sr_now_ms();
    SRLOG("ready: %s model, NPU grid %dx%d, %dx%d px a cell, upscaled %dx%d, in w_stride %u, out %u channel groups, zp %d scale %g, %d slots, "
          "native frame by a %s pass",
          sr->kind == 2 ? "cells" : sr->kind == 1 ? "space-to-depth" : "row5", sr->GW, sr->GH, sr->B, sr->B, sr->OW, sr->OH,
          sr->nin.w_stride, sr->nout.dims[1], sr->nout.zp, sr->nout.scale, SR_SLOTS, sr->nat ? "fragment" : "compute");
    return sr;
fail:
    SRLOG("NPU SR unavailable for this device");
    {   /* keep a record, so as not to try again, but with no context */
        int had = sr->have_ctx;
        sr_device_free(d);
        (void)had;
        d->sr = calloc(1, sizeof(SRDev));   /* have_ctx = 0: do not try again */
        if (d->sr) d->sr->d = d;
    }
    return NULL;
}

/* The decode pass (sr_decode.frag) and each slot's RGBA image of the upscaled frame, for an area of another size.
 * Once per device, when a swapchain first needs them: an area the frame's size (Azahar on the panel) does without
 * their memory. 0: not made (that swapchain resamples in sr_rotate.frag, as at first). */
static int sr_rgb_init(SRDev *sr)
{
    if (sr->rgb_on) return sr->rgb_on > 0;
    sr->rgb_on = -1;
    Dev *d = sr->d;
    VkDevice dv = d->device;
    for (int k = 0; k < SR_SLOTS; k++) {
        SRSlot *s = &sr->slot[k];
        VkImageCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D,
            .format = VK_FORMAT_R8G8B8A8_UNORM, .extent = { (uint32_t)sr->OW, (uint32_t)sr->OH, 1 }, .mipLevels = 1,
            .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
        SRVK(d->CreateImage(dv, &ii, NULL, &s->rgb));
        VkMemoryRequirements mr;
        d->GetImageMemoryRequirements(dv, s->rgb, &mr);
        VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = mr.size,
            .memoryTypeIndex = find_memory(d, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
        SRVK(d->AllocateMemory(dv, &mai, NULL, &s->grgb));
        SRVK(d->BindImageMemory(dv, s->rgb, s->grgb, 0));
        VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = s->rgb,
            .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = VK_FORMAT_R8G8B8A8_UNORM,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        SRVK(d->CreateImageView(dv, &vci, NULL, &s->vrgb));
        VkFramebufferCreateInfo fbi = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, .renderPass = sr->rp_off,
            .attachmentCount = 1, .pAttachments = &s->vrgb, .width = (uint32_t)sr->OW, .height = (uint32_t)sr->OH, .layers = 1 };
        SRVK(d->CreateFramebuffer(dv, &fbi, NULL, &s->fb_rgb));
    }
    /* sr_rect.frag: set0 = the slot's RGBA image; sr_decode.frag draws it from the frame's set (comp_ds) */
    VkDescriptorSetLayoutBinding rb = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &sr->linear };
    VkDescriptorSetLayoutCreateInfo dli = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &rb };
    SRVK(d->CreateDescriptorSetLayout(dv, &dli, NULL, &sr->rgb_dsl));
    VkPushConstantRange rpc = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(int32_t) };   /* rotate.vert's rot */
    VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &sr->rgb_dsl,
        .pushConstantRangeCount = 1, .pPushConstantRanges = &rpc };
    SRVK(d->CreatePipelineLayout(dv, &pli, NULL, &sr->rgb_pl));
    struct { int32_t B, GH, wstride, zp; float scale; } spc = {
        sr->B, sr->GH, (int32_t)(sr->nout.w_stride ? sr->nout.w_stride : (uint32_t)sr->GW), sr->nout.zp, sr->nout.scale };
    _Static_assert(sizeof spc == 5 * 4, "sr_decode.frag's constants are five 4-byte values");
    VkSpecializationMapEntry sme[5];
    for (uint32_t k = 0; k < 5; k++) sme[k] = (VkSpecializationMapEntry){ k, k * 4, 4 };
    VkSpecializationInfo spi = { 5, sme, sizeof spc, &spc };
    VkViewport dvp = { 0, 0, (float)sr->OW, (float)sr->OH, 0, 1 };
    SRVK(sr_gfx_pipe(d, sr->rp_off, sr->comp_pl, rotate_vert_spv, sizeof(rotate_vert_spv), sr_decode_frag_spv, sizeof(sr_decode_frag_spv),
                     &spi, dvp, 0, &sr->dec_pipe));
    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SR_SLOTS };
    VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = SR_SLOTS, .poolSizeCount = 1, .pPoolSizes = &ps };
    SRVK(d->CreateDescriptorPool(dv, &dpi, NULL, &sr->rgb_pool));
    for (int k = 0; k < SR_SLOTS; k++) {
        VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = sr->rgb_pool, .descriptorSetCount = 1, .pSetLayouts = &sr->rgb_dsl };
        SRVK(d->AllocateDescriptorSets(dv, &dai, &sr->rgb_ds[k]));
        VkDescriptorImageInfo ri = { sr->linear, sr->slot[k].vrgb, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet w = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = sr->rgb_ds[k], .dstBinding = 0, .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &ri };
        d->UpdateDescriptorSets(dv, 1, &w, 0, NULL);
    }
    sr->rgb_on = 1;
    SRLOG("decode pass: the upscaled %dx%d frame in RGBA once a new NPU output (a fragment pass), sampled once a pixel", sr->OW, sr->OH);
    return 1;
fail:
    SRLOG("decode pass unavailable: the area is resampled in each pixel");
    return 0;   /* what was made is freed with the device */
}

static void sr_swap_free(Swap *s)
{
    SRSwap *x = s->srs;
    if (!x) return;
    Dev *d = s->d;
    VkDevice dv = d->device;
    if (x->sr && x->sr->have_ctx) {   /* this swapchain's prep submissions may still run */
        VkFence f[SR_SLOTS];
        for (int k = 0; k < SR_SLOTS; k++) f[k] = x->sr->slot[k].fence;
        d->WaitForFences(dv, SR_SLOTS, f, VK_TRUE, 1000000000ull);
    }
    if (x->pool) d->DestroyCommandPool(dv, x->pool, NULL);
    if (x->pipe) d->DestroyPipeline(dv, x->pipe, NULL);
    if (x->bars_pipe) d->DestroyPipeline(dv, x->bars_pipe, NULL);
    if (x->ring_pipe) d->DestroyPipeline(dv, x->ring_pipe, NULL);
    if (x->pl) d->DestroyPipelineLayout(dv, x->pl, NULL);
    if (x->dp) d->DestroyDescriptorPool(dv, x->dp, NULL);
    free(x);
    s->srs = NULL;
}

/* The rotate + SR pipeline and this swapchain's prep descriptor sets. Breaks nothing when it fails. */
static void sr_swap_build(Swap *s, VkViewport vp, int scaled)
{
    sr_cfg();
    if (!g_sr.on) return;
    Dev *d = s->d;
    if (scaled) { SRLOG("scaled display mode: SR off for this swapchain"); return; }
    int AW = (int)s->app_ext.width, AH = (int)s->app_ext.height;
    if (AW < g_sr.W * g_sr.A || AH < g_sr.H * g_sr.A) {
        SRLOG("program image %dx%d smaller than x%d of %dx%d: SR off", AW, AH, g_sr.A, g_sr.W, g_sr.H);
        return;
    }
    if (s->fmt != VK_FORMAT_B8G8R8A8_UNORM && s->fmt != VK_FORMAT_R8G8B8A8_UNORM && s->fmt != VK_FORMAT_A8B8G8R8_UNORM_PACK32) {
        SRLOG("swapchain format %d is not a UNORM 8-bit RGBA one: SR off", s->fmt);
        return;
    }
    SRDev *sr = sr_device_get(d);
    if (!sr) return;
    SRSwap *x = calloc(1, sizeof *x);
    if (!x) return;
    x->sr = sr;
    x->ox = (AW - g_sr.W * g_sr.A) / 2;
    x->oy = (AH - g_sr.H * g_sr.A) / 2;
    if (g_sr.fit == 2) { x->dw = AW; x->dh = AH; }
    else if (g_sr.fit == 1) {   /* the frame's shape, as big as the picture allows */
        if ((long)AW * g_sr.H > (long)AH * g_sr.W) { x->dh = AH; x->dw = (int)(((long)g_sr.W * AH * 2 + g_sr.H) / (2L * g_sr.H)); }
        else { x->dw = AW; x->dh = (int)(((long)g_sr.H * AW * 2 + g_sr.W) / (2L * g_sr.W)); }
    } else { x->dw = g_sr.W * g_sr.A; x->dh = g_sr.H * g_sr.A; }
    x->dx = (AW - x->dw) / 2;
    x->dy = (AH - x->dh) / 2;
    /* With more than 1% of the native pixels in blocks not one colour the picture is shown as the program drew
     * it. Moved or resized, the upscaled frame would carry what a menu drew over the program's frame (the parts
     * of it one colour in blocks) somewhere else, beside the menu itself (PPSSPP's pause screen); and in place
     * too, a frame that is not the native one enlarged as expected (Azahar's large-and-small layout switched in
     * the game, a scale that is not whole) would come out as SR of the wrong pixels. A notification stays below
     * it and is handled block by block. */
    x->over = g_sr.over >= 0 ? g_sr.over : g_sr.W * g_sr.H / 100;
    x->moved = x->dx != x->ox || x->dy != x->oy || x->dw != g_sr.W * g_sr.A || x->dh != g_sr.H * g_sr.A;
    x->resample = (x->dw != sr->OW || x->dh != sr->OH) && sr_rgb_init(sr);
    VkDevice dv = d->device;

    /* on the display: the frame's rectangle, the bars around it, and under a moved frame the program's own bars */
    x->old = (x->dw != sr->OW || x->dh != sr->OH) && !x->resample;
    VkRect2D rect = sr_map_rect(s, x->dx, x->dy, x->dw, x->dh);
    {
        const int b[4][4] = { { 0, 0, x->dx, AH }, { x->dx + x->dw, 0, AW - x->dx - x->dw, AH },
                              { x->dx, 0, x->dw, x->dy }, { x->dx, x->dy + x->dh, x->dw, AH - x->dy - x->dh } };
        for (int k = 0; k < 4; k++)
            if (b[k][2] > 0 && b[k][3] > 0) x->bars[x->nbars++] = sr_map_rect(s, b[k][0], b[k][1], b[k][2], b[k][3]);
        /* inside the rectangle, outside the program's native frame */
        const int fx = x->ox, fy = x->oy, fw = g_sr.W * g_sr.A, fh = g_sr.H * g_sr.A;
        const int x0 = fx > x->dx ? fx : x->dx, x1 = fx + fw < x->dx + x->dw ? fx + fw : x->dx + x->dw;
        const int r[4][4] = { { x->dx, x->dy, fx - x->dx, x->dh }, { fx + fw, x->dy, x->dx + x->dw - fx - fw, x->dh },
                              { x0, x->dy, x1 - x0, fy - x->dy }, { x0, fy + fh, x1 - x0, x->dy + x->dh - fy - fh } };
        for (int k = 0; x->moved && k < 4; k++)
            if (r[k][2] > 0 && r[k][3] > 0) x->ring[x->nring++] = sr_map_rect(s, r[k][0], r[k][1], r[k][2], r[k][3]);
    }
    VkViewport rvp = { (float)rect.offset.x, (float)rect.offset.y, (float)rect.extent.width, (float)rect.extent.height, 0, 1 };
    /* the model's and the rectangle's values, fixed for the pipeline (as constants the divisions by B become
     * multiplies and the branches that do not apply go): sr_frame.frag takes the first seven, sr_rotate.frag all */
    struct { int32_t B, GH, wstride, zp; float scale; int32_t OW, OH; VkBool32 direct, moved; } spc = {
        sr->B, sr->GH, (int32_t)(sr->nout.w_stride ? sr->nout.w_stride : (uint32_t)sr->GW), sr->nout.zp, sr->nout.scale,
        sr->OW, sr->OH, x->dw == sr->OW && x->dh == sr->OH, (VkBool32)x->moved };
    _Static_assert(sizeof spc == 9 * 4, "the frame shaders' constants are nine 4-byte values");
    VkSpecializationMapEntry sme[9];
    for (uint32_t k = 0; k < 9; k++) sme[k] = (VkSpecializationMapEntry){ k, k * 4, 4 };
    if (x->old) {
        /* the whole picture by sr_rotate.frag: set0 = the program's picture, set1 = the slot's NPU output and colour */
        VkDescriptorSetLayout sets[2] = { s->dsl, sr->comp_dsl };
        VkPushConstantRange pc = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SRPush) };
        VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 2, .pSetLayouts = sets, .pushConstantRangeCount = 1, .pPushConstantRanges = &pc };
        SRVK(d->CreatePipelineLayout(dv, &pli, NULL, &x->pl));
        VkSpecializationInfo spi = { 9, sme, sizeof spc, &spc };
        SRVK(sr_gfx_pipe(d, s->rp, x->pl, rotate_vert_spv, sizeof(rotate_vert_spv), sr_rotate_frag_spv, sizeof(sr_rotate_frag_spv),
                         &spi, vp, 0, &x->pipe));
    } else {
        /* the bars around the frame: the plain rotate shader, scissored to them */
        if (x->nbars)
            SRVK(sr_gfx_pipe(d, s->rp, s->pl, rotate_vert_spv, sizeof(rotate_vert_spv), rotate_frag_spv, sizeof(rotate_frag_spv),
                             NULL, vp, 1, &x->bars_pipe));
        /* the frame, a rectangle of its own: the decoded image sampled, or built in each pixel where it is its own size */
        if (x->resample)
            SRVK(sr_gfx_pipe(d, s->rp, sr->rgb_pl, rotate_vert_spv, sizeof(rotate_vert_spv), sr_rect_frag_spv, sizeof(sr_rect_frag_spv),
                             NULL, rvp, 0, &x->pipe));
        else {
            VkSpecializationInfo spi = { 7, sme, sizeof spc, &spc };
            SRVK(sr_gfx_pipe(d, s->rp, sr->comp_pl, rotate_vert_spv, sizeof(rotate_vert_spv), sr_frame_frag_spv, sizeof(sr_frame_frag_spv),
                             &spi, rvp, 0, &x->pipe));
        }
        /* the program's bars under a moved frame: what is not black there, over the frame */
        if (x->nring)
            SRVK(sr_gfx_pipe(d, s->rp, s->pl, rotate_vert_spv, sizeof(rotate_vert_spv), sr_ring_frag_spv, sizeof(sr_ring_frag_spv),
                             NULL, vp, 1, &x->ring_pipe));
    }

    if (sr->nat) {
        /* sr_nat's set for each of the program's images */
        VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, s->n };
        VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = s->n, .poolSizeCount = 1, .pPoolSizes = &ps };
        SRVK(d->CreateDescriptorPool(dv, &dpi, NULL, &x->dp));
        for (uint32_t k = 0; k < s->n; k++) {
            VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = x->dp, .descriptorSetCount = 1, .pSetLayouts = &sr->tex_dsl };
            SRVK(d->AllocateDescriptorSets(dv, &dai, &x->nat_ds[k]));
            VkDescriptorImageInfo ai = { sr->nearest, s->app_view[k], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkWriteDescriptorSet w = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = x->nat_ds[k], .dstBinding = 0, .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &ai };
            d->UpdateDescriptorSets(dv, 1, &w, 0, NULL);
        }
    } else {
        VkDescriptorPoolSize ps[3] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, s->n * SR_SLOTS },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * s->n * SR_SLOTS }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s->n * SR_SLOTS } };
        VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = s->n * SR_SLOTS, .poolSizeCount = 3, .pPoolSizes = ps };
        SRVK(d->CreateDescriptorPool(dv, &dpi, NULL, &x->dp));
        for (uint32_t k = 0; k < s->n; k++) {
            for (int j = 0; j < SR_SLOTS; j++) {
                VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                    .descriptorPool = x->dp, .descriptorSetCount = 1, .pSetLayouts = &sr->prep_dsl };
                SRVK(d->AllocateDescriptorSets(dv, &dai, &x->prep_ds[k][j]));
                VkDescriptorImageInfo ai = { sr->nearest, s->app_view[k], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
                VkDescriptorBufferInfo bi = { sr->slot[j].bin, 0, VK_WHOLE_SIZE }, ni = { sr->slot[j].bcnt, 0, VK_WHOLE_SIZE };
                VkDescriptorImageInfo ci = { VK_NULL_HANDLE, sr->slot[j].vcbcr, VK_IMAGE_LAYOUT_GENERAL };
                VkWriteDescriptorSet w[4] = {
                    { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = x->prep_ds[k][j], .dstBinding = 0, .descriptorCount = 1,
                      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &ai },
                    { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = x->prep_ds[k][j], .dstBinding = 1, .descriptorCount = 1,
                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi },
                    { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = x->prep_ds[k][j], .dstBinding = 2, .descriptorCount = 1,
                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &ci },
                    { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = x->prep_ds[k][j], .dstBinding = 3, .descriptorCount = 1,
                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &ni } };
                d->UpdateDescriptorSets(dv, 4, w, 0, NULL);
            }
        }
    }
    VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = d->gfx_family };
    SRVK(d->CreateCommandPool(dv, &cpi, NULL, &x->pool));
    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = x->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = s->n };
    SRVK(d->AllocateCommandBuffers(dv, &cai, x->cb_prep));
    SRVK(d->AllocateCommandBuffers(dv, &cai, x->cb_pres));
    for (uint32_t k = 0; k < s->n; k++)
        if (d->SetLoaderData) { d->SetLoaderData(dv, x->cb_prep[k]); d->SetLoaderData(dv, x->cb_pres[k]); }
    s->srs = x;
    SRLOG("swapchain %dx%d: SR region %dx%d at (%d,%d), shown %dx%d at (%d,%d)%s, as drawn over %d blocks not one colour",
          AW, AH, g_sr.W * g_sr.A, g_sr.H * g_sr.A, x->ox, x->oy, x->dw, x->dh, x->dx, x->dy,
          x->resample ? " (decoded once, resampled)" : x->old ? " (resampled in each pixel)" : "", x->over);
    return;
fail:
    s->srs = x;
    sr_swap_free(s);
}

static void sr_stats(SRDev *sr)
{
    double t = sr_now_ms();
    if (!g_sr.stats || t - sr->t_last < 5000.0) return;
    pthread_mutex_lock(&sr->m);
    unsigned long np = sr->n_present - sr->last_present, nn = sr->n_npu - sr->last_npu;
    double avg = sr->n_npu ? sr->npu_ms / sr->n_npu : 0;
    long blocks = sr->ready >= 0 && sr->slot[sr->ready].mcnt ? (long)sr->slot[sr->ready].mcnt[0] : -1;
    SRLOG("%.1f s: present %.1f/s, NPU %.1f/s (avg %.2f ms), prep %lu, skipped %lu, failed %lu, not one colour %ld, decoded %lu",
          (t - sr->t_last) / 1000.0, np * 1000.0 / (t - sr->t_last), nn * 1000.0 / (t - sr->t_last), avg, sr->n_prep,
          sr->n_skip, sr->n_fail, blocks, sr->n_dec);
    sr->last_present = sr->n_present; sr->last_npu = sr->n_npu; sr->t_last = t;
    pthread_mutex_unlock(&sr->m);
}

/* present with SR: 1 = handled (the result in *res), 0 = the usual path runs */
static int sr_present(Dev *d, Swap *s, VkQueue queue, const VkPresentInfoKHR *pi, uint32_t idx, VkResult *res)
{
    SRSwap *x = s->srs;
    if (!x) return 0;
    SRDev *sr = x->sr;
    VkDevice dv = d->device;
    d->WaitForFences(dv, 1, &s->fence[idx], VK_TRUE, UINT64_MAX);
    d->ResetFences(dv, 1, &s->fence[idx]);

    pthread_mutex_lock(&sr->m);
    int ready = sr->ready, w = -1;
    unsigned rgen = ready >= 0 ? sr->slot[ready].gen : 0;
    unsigned long pno = ++sr->n_present;
    if (sr->pending < 0) {
        for (int k = 0; k < SR_SLOTS; k++)
            if (k != sr->busy && k != ready) { w = k; break; }
    } else sr->n_skip++;
    pthread_mutex_unlock(&sr->m);

    VkPipelineStageFlags stages[16];
    uint32_t nw = pi->waitSemaphoreCount < 16 ? pi->waitSemaphoreCount : 16;
    for (uint32_t k = 0; k < nw; k++) stages[k] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkImageMemoryBarrier ib = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = s->app[idx], .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT };

    if (w >= 0) {
        /* prep: a submission of its own (the worker waits for its fence); it waits for what the program's frame waited for */
        VkCommandBuffer cb = x->cb_prep[idx];
        d->ResetCommandBuffer(cb, 0);
        d->BeginCommandBuffer(cb, &bi);
        d->CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, NULL, 0, NULL, 1, &ib);
        if (sr->nat) {
            /* the native frame (colour, one-colour flags, luma) by a fragment pass into the slot's image */
            VkRenderPassBeginInfo nbi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = sr->rp_off,
                .framebuffer = sr->slot[w].fb_cbcr, .renderArea = { { 0, 0 }, { (uint32_t)g_sr.W, (uint32_t)g_sr.H } } };
            d->CmdBeginRenderPass(cb, &nbi, VK_SUBPASS_CONTENTS_INLINE);
            d->CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sr->nat_pipe);
            d->CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sr->nat_pl, 0, 1, &x->nat_ds[idx], 0, NULL);
            int32_t np[7] = { x->ox, x->oy, g_sr.W, g_sr.H, g_sr.A, (int32_t)s->app_ext.width, (int32_t)s->app_ext.height };
            d->CmdPushConstants(cb, sr->nat_pl, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof np, np);
            d->CmdDraw(cb, 3, 1, 0, 0);
            d->CmdEndRenderPass(cb);
        } else {
            VkImageMemoryBarrier cbb = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_SHADER_READ_BIT, .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .oldLayout = sr->slot[w].cbcr_init ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = sr->slot[w].cbcr, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
            d->CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &cbb);
            sr->slot[w].cbcr_init = 1;
        }
        d->CmdFillBuffer(cb, sr->slot[w].bcnt, 0, 16, 0);   /* the count of blocks not one colour, from 0 */
        VkMemoryBarrier fb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT };
        d->CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &fb, 0, NULL, 0, NULL);
        const int32_t in_stride = (int32_t)(sr->nin.w_stride ? sr->nin.w_stride : (uint32_t)sr->GW);
        if (sr->nat) {
            /* the NPU's input from the native frame, a 2x2 cell an invocation */
            d->CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, sr->nin_pipe);
            d->CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, sr->nin_pl, 0, 1, &sr->nin_ds[w], 0, NULL);
            struct { int32_t GW, GH, wstride; float iw, ih; } np = { sr->GW, sr->GH, in_stride, 1.0f / (float)g_sr.W, 1.0f / (float)g_sr.H };
            d->CmdPushConstants(cb, sr->nin_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof np, &np);
        } else {
            d->CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, sr->prep_pipe);
            d->CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, sr->prep_pl, 0, 1, &x->prep_ds[idx][w], 0, NULL);
            /* the prep shaders' S is the step in the program's picture, K the cell (sr_prep_cell.comp) */
            int32_t pp[7] = { x->ox, x->oy, g_sr.W, g_sr.H, g_sr.A, in_stride, sr->K };
            d->CmdPushConstants(cb, sr->prep_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pp, pp);
        }
        d->CmdDispatch(cb, (uint32_t)(sr->GW + 15) / 16, (uint32_t)(sr->GH + 15) / 16, 1);   /* row5: a pixel, s2d: a 2x2 cell */
        VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_READ_BIT };
        d->CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
        d->EndCommandBuffer(cb);
        d->ResetFences(dv, 1, &sr->slot[w].fence);
        sr->slot[w].pno = pno;
        VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = nw, .pWaitSemaphores = pi->pWaitSemaphores,
            .pWaitDstStageMask = stages, .commandBufferCount = 1, .pCommandBuffers = &cb };
        VkResult r = d->QueueSubmit(queue, 1, &si, sr->slot[w].fence);
        if (r != VK_SUCCESS) {
            /* both fences were reset for submissions that did not happen: signalled by empty ones, or the next present
             * of this image and the swapchain's destruction would wait on them for ever */
            SRLOG("prep submit failed: %d", r);
            d->QueueSubmit(queue, 0, NULL, sr->slot[w].fence);
            d->QueueSubmit(queue, 0, NULL, s->fence[idx]);
            *res = r;
            return 1;
        }
        pthread_mutex_lock(&sr->m);
        sr->pending = w;
        sr->n_prep++;
        pthread_cond_signal(&sr->cv);
        pthread_mutex_unlock(&sr->m);
    }

    /* the rotate pass: with SR from the latest finished slot, else as usual */
    VkCommandBuffer cb = x->cb_pres[idx];
    d->ResetCommandBuffer(cb, 0);
    d->BeginCommandBuffer(cb, &bi);
    if (w < 0)
        d->CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &ib);
    VkMemoryBarrier ext = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT };
    d->CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &ext, 0, NULL, 0, NULL);
    /* From the count of blocks not one colour in the slot shown (its prep has run - the NPU had its input - so
     * the count is final): above the limit (a full menu, a frame not as expected) the program's picture as it
     * is, else the upscale (a notification over the frame upscaled with it: the owner, 2026-11-23, it reads
     * badly either way). Decided here once a frame rather than in each of the panel's pixels. */
    long nflat = ready >= 0 && sr->cnt_host ? (long)sr->slot[ready].mcnt[0] : -1;
    int use_sr = ready >= 0 && pno - sr->slot[ready].pno <= SR_MAXAGE && !(nflat >= 0 && x->over > 0 && nflat > x->over);
    int32_t rot = s->rot;
    if (use_sr && x->resample && sr->slot[ready].dec_gen != rgen) {
        /* a new NPU output: the upscaled frame into the slot's RGBA image, once (a pass of its own) */
        SRSlot *sl = &sr->slot[ready];
        VkRenderPassBeginInfo dbi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = sr->rp_off,
            .framebuffer = sl->fb_rgb, .renderArea = { { 0, 0 }, { (uint32_t)sr->OW, (uint32_t)sr->OH } } };
        d->CmdBeginRenderPass(cb, &dbi, VK_SUBPASS_CONTENTS_INLINE);
        d->CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sr->dec_pipe);
        d->CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, sr->comp_pl, 0, 1, &sr->comp_ds[ready], 0, NULL);
        int32_t rot0 = 0;   /* rotate.vert: uv 0..1 over the image, not turned */
        d->CmdPushConstants(cb, sr->comp_pl, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof rot0, &rot0);
        d->CmdDraw(cb, 3, 1, 0, 0);
        d->CmdEndRenderPass(cb);
        sl->dec_gen = rgen;
        sr->n_dec++;
    }
    VkRenderPassBeginInfo rbi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = s->rp, .framebuffer = s->fb[idx],
        .renderArea = { { 0, 0 }, s->real_ext } };
    d->CmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    if (use_sr && x->old) {
        d->CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, x->pipe);
        VkDescriptorSet ds2[2] = { s->ds[idx], sr->comp_ds[ready] };
        d->CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, x->pl, 0, 2, ds2, 0, NULL);
        SRPush p = { s->rot, (int32_t)s->app_ext.width, (int32_t)s->app_ext.height, x->ox, x->oy, g_sr.W, g_sr.H, g_sr.S,
                     (int32_t)(sr->nout.w_stride ? sr->nout.w_stride : (uint32_t)sr->GW), sr->nout.zp, sr->nout.scale, sr->B, sr->GH,
                     g_sr.A, x->dx, x->dy, x->dw, x->dh, sr->OW, sr->OH, x->moved };
        d->CmdPushConstants(cb, x->pl, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof p, &p);
        d->CmdDraw(cb, 3, 1, 0, 0);
    } else if (use_sr) {
        /* the bars around the frame as the program drew them */
        if (x->nbars) {
            d->CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, x->bars_pipe);
            d->CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pl, 0, 1, &s->ds[idx], 0, NULL);
            d->CmdPushConstants(cb, s->pl, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof rot, &rot);
            for (uint32_t k = 0; k < x->nbars; k++) {
                d->CmdSetScissor(cb, 0, 1, &x->bars[k]);
                d->CmdDraw(cb, 3, 1, 0, 0);
            }
        }
        /* the frame: a rectangle of its own */
        VkPipelineLayout fpl = x->resample ? sr->rgb_pl : sr->comp_pl;
        d->CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, x->pipe);
        d->CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, fpl, 0, 1, x->resample ? &sr->rgb_ds[ready] : &sr->comp_ds[ready], 0, NULL);
        d->CmdPushConstants(cb, fpl, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof rot, &rot);
        d->CmdDraw(cb, 3, 1, 0, 0);
        /* moved, it covers the program's bars: what is not black there is drawn over it */
        if (x->nring) {
            d->CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, x->ring_pipe);
            d->CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pl, 0, 1, &s->ds[idx], 0, NULL);
            d->CmdPushConstants(cb, s->pl, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof rot, &rot);
            for (uint32_t k = 0; k < x->nring; k++) {
                d->CmdSetScissor(cb, 0, 1, &x->ring[k]);
                d->CmdDraw(cb, 3, 1, 0, 0);
            }
        }
    } else {
        d->CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pipe);
        d->CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pl, 0, 1, &s->ds[idx], 0, NULL);
        d->CmdPushConstants(cb, s->pl, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof rot, &rot);
        d->CmdDraw(cb, 3, 1, 0, 0);
    }
    d->CmdEndRenderPass(cb);
    ib.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; ib.dstAccessMask = 0;
    ib.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; ib.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    d->CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &ib);
    d->EndCommandBuffer(cb);
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = w >= 0 ? 0 : nw, .pWaitSemaphores = w >= 0 ? NULL : pi->pWaitSemaphores, .pWaitDstStageMask = stages,
        .commandBufferCount = 1, .pCommandBuffers = &cb, .signalSemaphoreCount = 1, .pSignalSemaphores = &s->done[idx] };
    VkResult r = d->QueueSubmit(queue, 1, &si, s->fence[idx]);
    if (r != VK_SUCCESS) {
        SRLOG("present pass submit failed: %d", r);
        d->QueueSubmit(queue, 0, NULL, s->fence[idx]);   /* reset above, never submitted: signalled by an empty one */
        *res = r;
        return 1;
    }
    sr_stats(sr);
    VkPresentInfoKHR p2 = *pi;
    p2.waitSemaphoreCount = 1;
    p2.pWaitSemaphores = &s->done[idx];
    *res = d->QueuePresentKHR(queue, &p2);
    return 1;
}
