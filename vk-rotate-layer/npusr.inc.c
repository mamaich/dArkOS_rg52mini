/*
 * npusr.inc.c - NPU UPSCALE: a frame enlarged by a neural network (ESPCN) on the RK3562's NPU, inside the
 * rotate layer. Included by darkos_rotate_layer.c; it does anything only when asked:
 *
 *   DARKOS_NPU_SR=x3:400x240   the scale and the emulator's native frame size (its output must be that frame
 *                              enlarged by an integer, nearest, centred in the program's picture, as
 *                              RetroArch's integer scale has it)
 *   DARKOS_NPU_SR_MODEL=...    the .rknn model (default /usr/share/darkos/npusr/espcn_x<S>_<W>x<H>.rknn)
 *   DARKOS_NPU_SR_LIB=...      librknnrt.so (default: found by dlopen)
 *   DARKOS_NPU_SR_STATS=1      counters on stderr every 5 s
 *
 * How: at present a compute pass, in a submission of its own, takes every S-th pixel of the area from the
 * program's picture and writes the NPU's input (5 luma channels, its native NC1HWC2 int8 layout) and the
 * colour into a free slot; a worker thread waits for that submission's fence and runs the NPU; the rotate
 * pass builds each pixel inside the area from the latest finished NPU output and the colour. Three slots
 * (the NPU's / the latest finished / being written); present never waits for the NPU: when the NPU is busy
 * and a frame waits already, no new one is queued. The NPU allocates the memory and Vulkan imports it
 * (dma-buf). Written by the AI-SR session (2026-11-22/23); docs/KNOWN-ISSUES.md, "NPU UPSCALE".
 */
#include <dlfcn.h>
#include <time.h>
#include <unistd.h>
#include "rknn_api.h"
#include "sr_spv.h"   /* sr_prep_spv[], sr_rotate_frag_spv[] */

#define SR_SLOTS 3
#define SRLOG(...) do { fprintf(stderr, "darkos_npusr: " __VA_ARGS__); fputc('\n', stderr); } while (0)

typedef struct {
    int on, S, W, H, stats;
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
    int S = 0, W = 0, H = 0;
    if (sscanf(e, "x%d:%dx%d", &S, &W, &H) != 3 || S < 2 || S > 4 || W < 16 || H < 16 || W > 2048 || H > 2048) {
        SRLOG("DARKOS_NPU_SR=\"%s\" not understood (expected e.g. x3:400x240), off", e);
        return;
    }
    g_sr.S = S; g_sr.W = W; g_sr.H = H;
    const char *m = getenv("DARKOS_NPU_SR_MODEL");
    if (m && *m) snprintf(g_sr.model, sizeof g_sr.model, "%s", m);
    else snprintf(g_sr.model, sizeof g_sr.model, "/usr/share/darkos/npusr/espcn_x%d_%dx%d.rknn", S, W, H);
    const char *l = getenv("DARKOS_NPU_SR_LIB");
    snprintf(g_sr.lib, sizeof g_sr.lib, "%s", (l && *l) ? l : "librknnrt.so");
    const char *st = getenv("DARKOS_NPU_SR_STATS");
    g_sr.stats = st && *st && *st != '0';
    g_sr.on = 1;
    SRLOG("on: x%d, %dx%d, model %s", S, W, H, g_sr.model);
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
    VkFence fence;          /* this slot's prep has run (the worker waits for it) */
    int cbcr_init;          /* the image is in GENERAL already */
} SRSlot;

typedef struct SRDev {
    Dev *d;
    void *lib;
    RknnApi r;
    rknn_context ctx;
    int have_ctx;
    rknn_tensor_attr nin, nout;
    SRSlot slot[SR_SLOTS];
    VkSampler nearest, linear;
    VkDescriptorSetLayout prep_dsl, comp_dsl;
    VkPipelineLayout prep_pl;
    VkPipeline prep_pipe;
    VkDescriptorPool dpool;
    VkDescriptorSet comp_ds[SR_SLOTS];
    pthread_t th;
    int th_started;
    pthread_mutex_t m;
    pthread_cond_t cv;
    int quit, pending, busy, ready;
    /* counters */
    unsigned long n_present, n_prep, n_npu, n_skip, n_fail;
    double npu_ms, t_last;
    unsigned long last_present, last_npu;
} SRDev;

/* what depends on the swapchain */
typedef struct SRSwap {
    SRDev *sr;
    int ox, oy;
    VkPipelineLayout pl;
    VkPipeline pipe;
    VkDescriptorPool dp;
    VkDescriptorSet prep_ds[MAX_IMAGES][SR_SLOTS];
    VkCommandPool pool;
    VkCommandBuffer cb_prep[MAX_IMAGES], cb_pres[MAX_IMAGES];
} SRSwap;

typedef struct { int32_t rot, AW, AH, ox, oy, W, H, S, wstride, zp; float scale; } SRPush;

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
        if (d->WaitForFences(d->device, 1, &sr->slot[k].fence, VK_TRUE, 1000000000ull) == VK_SUCCESS) {
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
        if (ok) { sr->ready = k; sr->n_npu++; sr->npu_ms += dt; }
        else sr->n_fail++;
        pthread_mutex_unlock(&sr->m);
    }
    return NULL;
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
            d->sr = NULL;   /* free nothing under a live thread */
            return;
        }
    }
    VkDevice dv = d->device;
    d->DeviceWaitIdle(dv);
    for (int k = 0; k < SR_SLOTS; k++) {
        SRSlot *s = &sr->slot[k];
        if (s->fence) d->DestroyFence(dv, s->fence, NULL);
        if (s->vcbcr) d->DestroyImageView(dv, s->vcbcr, NULL);
        if (s->cbcr) d->DestroyImage(dv, s->cbcr, NULL);
        if (s->gcbcr) d->FreeMemory(dv, s->gcbcr, NULL);
        if (s->bin) d->DestroyBuffer(dv, s->bin, NULL);
        if (s->bout) d->DestroyBuffer(dv, s->bout, NULL);
        if (s->gin) d->FreeMemory(dv, s->gin, NULL);
        if (s->gout) d->FreeMemory(dv, s->gout, NULL);
        if (sr->have_ctx && s->min) sr->r.destroy_mem(sr->ctx, s->min);
        if (sr->have_ctx && s->mout) sr->r.destroy_mem(sr->ctx, s->mout);
    }
    if (sr->prep_pipe) d->DestroyPipeline(dv, sr->prep_pipe, NULL);
    if (sr->prep_pl) d->DestroyPipelineLayout(dv, sr->prep_pl, NULL);
    if (sr->dpool) d->DestroyDescriptorPool(dv, sr->dpool, NULL);
    if (sr->prep_dsl) d->DestroyDescriptorSetLayout(dv, sr->prep_dsl, NULL);
    if (sr->comp_dsl) d->DestroyDescriptorSetLayout(dv, sr->comp_dsl, NULL);
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
    /* ESPCN row5 expected: input NC1HWC2 1x1xHxWx16 int8 (5 channels), output NC1HWC2 1x1xHxWx16 int8 (S*S channels) */
    if (sr->nin.fmt != RKNN_TENSOR_NC1HWC2 || sr->nin.n_dims != 5 || sr->nin.dims[1] != 1 || sr->nin.dims[4] != 16 ||
        (int)sr->nin.dims[2] != g_sr.H || (int)sr->nin.dims[3] != g_sr.W ||
        sr->nout.fmt != RKNN_TENSOR_NC1HWC2 || sr->nout.n_dims != 5 || sr->nout.dims[1] != 1 || sr->nout.dims[4] != 16 ||
        (int)sr->nout.dims[2] != g_sr.H || (int)sr->nout.dims[3] != g_sr.W || g_sr.S * g_sr.S > 16) {
        SRLOG("model tensors do not match x%d:%dx%d (in %ux%ux%ux%ux%u fmt %d, out %ux%ux%ux%ux%u fmt %d)", g_sr.S, g_sr.W, g_sr.H,
              sr->nin.dims[0], sr->nin.dims[1], sr->nin.dims[2], sr->nin.dims[3], sr->nin.dims[4], sr->nin.fmt,
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
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
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
        VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
        SRVK(d->CreateFence(dv, &fi, NULL, &s->fence));
    }

    /* prep: set0 = the program's picture, the slot's NPU input, the slot's colour */
    VkDescriptorSetLayoutBinding pb[3] = {
        { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &sr->nearest },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL },
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL } };
    VkDescriptorSetLayoutCreateInfo dli = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 3, .pBindings = pb };
    SRVK(d->CreateDescriptorSetLayout(dv, &dli, NULL, &sr->prep_dsl));
    VkPushConstantRange ppc = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 6 * sizeof(int32_t) };
    VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &sr->prep_dsl, .pushConstantRangeCount = 1, .pPushConstantRanges = &ppc };
    SRVK(d->CreatePipelineLayout(dv, &pli, NULL, &sr->prep_pl));
    VkShaderModuleCreateInfo smi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = sizeof(sr_prep_spv), .pCode = sr_prep_spv };
    VkShaderModule cs;
    SRVK(d->CreateShaderModule(dv, &smi, NULL, &cs));
    VkComputePipelineCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = cs, .pName = "main" },
        .layout = sr->prep_pl };
    VkResult pr = d->CreateComputePipelines(dv, VK_NULL_HANDLE, 1, &cpi, NULL, &sr->prep_pipe);
    d->DestroyShaderModule(dv, cs, NULL);
    SRVK(pr);

    /* compose (in the rotate fragment shader): set1 = the slot's NPU output + the slot's colour */
    VkDescriptorSetLayoutBinding cb[2] = {
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL },
        { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &sr->linear } };
    dli.bindingCount = 2; dli.pBindings = cb;
    SRVK(d->CreateDescriptorSetLayout(dv, &dli, NULL, &sr->comp_dsl));
    VkDescriptorPoolSize ps[2] = { { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, SR_SLOTS }, { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, SR_SLOTS } };
    VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = SR_SLOTS, .poolSizeCount = 2, .pPoolSizes = ps };
    SRVK(d->CreateDescriptorPool(dv, &dpi, NULL, &sr->dpool));
    for (int k = 0; k < SR_SLOTS; k++) {
        VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = sr->dpool, .descriptorSetCount = 1, .pSetLayouts = &sr->comp_dsl };
        SRVK(d->AllocateDescriptorSets(dv, &dai, &sr->comp_ds[k]));
        VkDescriptorBufferInfo bi = { sr->slot[k].bout, 0, VK_WHOLE_SIZE };
        VkDescriptorImageInfo ci = { sr->linear, sr->slot[k].vcbcr, VK_IMAGE_LAYOUT_GENERAL };
        VkWriteDescriptorSet w[2] = {
            { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = sr->comp_ds[k], .dstBinding = 0, .descriptorCount = 1,
              .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi },
            { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = sr->comp_ds[k], .dstBinding = 1, .descriptorCount = 1,
              .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &ci } };
        d->UpdateDescriptorSets(dv, 2, w, 0, NULL);
    }
    SRCHK(pthread_create(&sr->th, NULL, sr_worker, sr) == 0);
    sr->th_started = 1;
    sr->t_last = sr_now_ms();
    SRLOG("ready: NPU in %ux%ux%u (w_stride %u), out zp %d scale %g, %d slots", sr->nin.dims[2], sr->nin.dims[3], sr->nin.dims[4],
          sr->nin.w_stride, sr->nout.zp, sr->nout.scale, SR_SLOTS);
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
    if (AW < g_sr.W * g_sr.S || AH < g_sr.H * g_sr.S) {
        SRLOG("program image %dx%d smaller than x%d of %dx%d: SR off", AW, AH, g_sr.S, g_sr.W, g_sr.H);
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
    x->ox = (AW - g_sr.W * g_sr.S) / 2;
    x->oy = (AH - g_sr.H * g_sr.S) / 2;
    VkDevice dv = d->device;

    VkDescriptorSetLayout sets[2] = { s->dsl, sr->comp_dsl };
    VkPushConstantRange pc = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SRPush) };
    VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2, .pSetLayouts = sets, .pushConstantRangeCount = 1, .pPushConstantRanges = &pc };
    SRVK(d->CreatePipelineLayout(dv, &pli, NULL, &x->pl));
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = sizeof(rotate_vert_spv), .pCode = rotate_vert_spv };
    SRVK(d->CreateShaderModule(dv, &smi, NULL, &vs));
    smi.codeSize = sizeof(sr_rotate_frag_spv); smi.pCode = sr_rotate_frag_spv;
    if (d->CreateShaderModule(dv, &smi, NULL, &fs) != VK_SUCCESS) { d->DestroyShaderModule(dv, vs, NULL); goto fail; }
    VkPipelineShaderStageCreateInfo st[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main" } };
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
    VkGraphicsPipelineCreateInfo gpi = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = st, .pVertexInputState = &vi, .pInputAssemblyState = &ia,
        .pViewportState = &vps, .pRasterizationState = &rs, .pMultisampleState = &ms,
        .pColorBlendState = &cbs, .layout = x->pl, .renderPass = s->rp, .subpass = 0 };
    VkResult pr = d->CreateGraphicsPipelines(dv, VK_NULL_HANDLE, 1, &gpi, NULL, &x->pipe);
    d->DestroyShaderModule(dv, vs, NULL);
    d->DestroyShaderModule(dv, fs, NULL);
    SRVK(pr);

    VkDescriptorPoolSize ps[3] = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, s->n * SR_SLOTS },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, s->n * SR_SLOTS }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, s->n * SR_SLOTS } };
    VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = s->n * SR_SLOTS, .poolSizeCount = 3, .pPoolSizes = ps };
    SRVK(d->CreateDescriptorPool(dv, &dpi, NULL, &x->dp));
    for (uint32_t k = 0; k < s->n; k++) {
        for (int j = 0; j < SR_SLOTS; j++) {
            VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .descriptorPool = x->dp, .descriptorSetCount = 1, .pSetLayouts = &sr->prep_dsl };
            SRVK(d->AllocateDescriptorSets(dv, &dai, &x->prep_ds[k][j]));
            VkDescriptorImageInfo ai = { sr->nearest, s->app_view[k], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            VkDescriptorBufferInfo bi = { sr->slot[j].bin, 0, VK_WHOLE_SIZE };
            VkDescriptorImageInfo ci = { VK_NULL_HANDLE, sr->slot[j].vcbcr, VK_IMAGE_LAYOUT_GENERAL };
            VkWriteDescriptorSet w[3] = {
                { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = x->prep_ds[k][j], .dstBinding = 0, .descriptorCount = 1,
                  .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &ai },
                { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = x->prep_ds[k][j], .dstBinding = 1, .descriptorCount = 1,
                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi },
                { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = x->prep_ds[k][j], .dstBinding = 2, .descriptorCount = 1,
                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &ci } };
            d->UpdateDescriptorSets(dv, 3, w, 0, NULL);
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
    SRLOG("swapchain %dx%d: SR region %dx%d at (%d,%d)", AW, AH, g_sr.W * g_sr.S, g_sr.H * g_sr.S, x->ox, x->oy);
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
    SRLOG("%.1f s: present %.1f/s, NPU %.1f/s (avg %.2f ms), prep %lu, skipped %lu, failed %lu", (t - sr->t_last) / 1000.0,
          np * 1000.0 / (t - sr->t_last), nn * 1000.0 / (t - sr->t_last), avg, sr->n_prep, sr->n_skip, sr->n_fail);
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
    sr->n_present++;
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
        VkImageMemoryBarrier cbb = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT, .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .oldLayout = sr->slot[w].cbcr_init ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = sr->slot[w].cbcr, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        d->CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &cbb);
        sr->slot[w].cbcr_init = 1;
        d->CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, sr->prep_pipe);
        d->CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, sr->prep_pl, 0, 1, &x->prep_ds[idx][w], 0, NULL);
        int32_t pp[6] = { x->ox, x->oy, g_sr.W, g_sr.H, g_sr.S, (int32_t)(sr->nin.w_stride ? sr->nin.w_stride : (uint32_t)g_sr.W) };
        d->CmdPushConstants(cb, sr->prep_pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pp, pp);
        d->CmdDispatch(cb, (uint32_t)(g_sr.W + 15) / 16, (uint32_t)(g_sr.H + 15) / 16, 1);
        VkMemoryBarrier mb = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_READ_BIT };
        d->CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
        d->EndCommandBuffer(cb);
        d->ResetFences(dv, 1, &sr->slot[w].fence);
        VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .waitSemaphoreCount = nw, .pWaitSemaphores = pi->pWaitSemaphores,
            .pWaitDstStageMask = stages, .commandBufferCount = 1, .pCommandBuffers = &cb };
        VkResult r = d->QueueSubmit(queue, 1, &si, sr->slot[w].fence);
        if (r != VK_SUCCESS) { SRLOG("prep submit failed: %d", r); *res = r; return 1; }
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
    d->CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 1, &ext, 0, NULL, 0, NULL);
    VkRenderPassBeginInfo rbi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, .renderPass = s->rp, .framebuffer = s->fb[idx],
        .renderArea = { { 0, 0 }, s->real_ext } };
    d->CmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    if (ready >= 0) {
        d->CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, x->pipe);
        VkDescriptorSet ds2[2] = { s->ds[idx], sr->comp_ds[ready] };
        d->CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, x->pl, 0, 2, ds2, 0, NULL);
        SRPush p = { g_rot, (int32_t)s->app_ext.width, (int32_t)s->app_ext.height, x->ox, x->oy, g_sr.W, g_sr.H, g_sr.S,
                     (int32_t)(sr->nout.w_stride ? sr->nout.w_stride : (uint32_t)g_sr.W), sr->nout.zp, sr->nout.scale };
        d->CmdPushConstants(cb, x->pl, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof p, &p);
    } else {
        d->CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pipe);
        d->CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pl, 0, 1, &s->ds[idx], 0, NULL);
        int32_t rot = g_rot;
        d->CmdPushConstants(cb, s->pl, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof rot, &rot);
    }
    d->CmdDraw(cb, 3, 1, 0, 0);
    d->CmdEndRenderPass(cb);
    ib.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; ib.dstAccessMask = 0;
    ib.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; ib.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    d->CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &ib);
    d->EndCommandBuffer(cb);
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = w >= 0 ? 0 : nw, .pWaitSemaphores = w >= 0 ? NULL : pi->pWaitSemaphores, .pWaitDstStageMask = stages,
        .commandBufferCount = 1, .pCommandBuffers = &cb, .signalSemaphoreCount = 1, .pSignalSemaphores = &s->done[idx] };
    VkResult r = d->QueueSubmit(queue, 1, &si, s->fence[idx]);
    if (r != VK_SUCCESS) { SRLOG("present pass submit failed: %d", r); *res = r; return 1; }
    sr_stats(sr);
    VkPresentInfoKHR p2 = *pi;
    p2.waitSemaphoreCount = 1;
    p2.pWaitSemaphores = &s->done[idx];
    *res = d->QueuePresentKHR(queue, &p2);
    return 1;
}
