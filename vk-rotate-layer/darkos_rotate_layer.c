/*
 * VK_LAYER_DARKOS_rotate - present on a portrait display panel as if it were
 * landscape.
 *
 * The RG52 Mini's DSI panel is 720x1280, mounted sideways. GL programs get a
 * landscape picture through SDL, which turns every frame with the RGA, but a
 * Vulkan program presenting through VK_KHR_display (SDL's KMSDRM Vulkan path,
 * RetroArch's khr_display context) gets the panel as it is and draws a
 * stretched, sideways picture.
 *
 * This implicit layer makes such a display look landscape and turns the
 * frames itself:
 *  - display properties, display modes, display plane capabilities and the
 *    capabilities of a surface made on such a mode report width and height
 *    swapped (1280x720);
 *  - a display plane surface or display mode created with a landscape extent
 *    is created portrait underneath;
 *  - a swapchain on such a surface is created portrait; the program gets
 *    landscape images of the layer's own to render to, and at present time a
 *    small graphics pass draws each one turned by 90 degrees into the real
 *    swapchain image.
 *
 * Displays that are landscape already are left alone, so the layer does
 * nothing on other devices. DARKOS_VK_ROTATE_DISABLE=1 turns it off (the
 * loader does that), DARKOS_VK_ROTATE=270 turns the other way,
 * DARKOS_VK_ROTATE_DEBUG=1 logs to stderr.
 */
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rotate_spv.h"   /* rotate_vert_spv[], rotate_frag_spv[] */

#define LAYER_NAME "VK_LAYER_DARKOS_rotate"
#define EXPORT __attribute__((visibility("default")))
#define MAX_IMAGES 8

static int g_debug = -1;
static int g_rot = 90;

#define LOG(...) do { if (g_debug > 0) { fprintf(stderr, "darkos_rotate: " __VA_ARGS__); fputc('\n', stderr); } } while (0)

static void init_env(void)
{
    if (g_debug >= 0)
        return;
    const char *e = getenv("DARKOS_VK_ROTATE_DEBUG");
    g_debug = (e && *e && *e != '0') ? 1 : 0;
    e = getenv("DARKOS_VK_ROTATE");
    if (e && atoi(e) == 270)
        g_rot = 270;
}

/* ------------------------------------------------------------------------ */
/* Bookkeeping: small lists under one lock. Dispatchable handles are keyed by
   the loader's dispatch table pointer, the first word of the object. */

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static void *dkey(const void *h) { return *(void *const *)h; }

typedef struct Inst {
    struct Inst *next;
    void *key;
    VkInstance instance;
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkDestroyInstance DestroyInstance;
    PFN_vkGetPhysicalDeviceDisplayPropertiesKHR GetDisplayProps;
    PFN_vkGetPhysicalDeviceDisplayProperties2KHR GetDisplayProps2;
    PFN_vkGetDisplayModePropertiesKHR GetModeProps;
    PFN_vkGetDisplayModeProperties2KHR GetModeProps2;
    PFN_vkCreateDisplayModeKHR CreateDisplayMode;
    PFN_vkGetDisplayPlaneCapabilitiesKHR GetPlaneCaps;
    PFN_vkGetDisplayPlaneCapabilities2KHR GetPlaneCaps2;
    PFN_vkCreateDisplayPlaneSurfaceKHR CreateDisplayPlaneSurface;
    PFN_vkDestroySurfaceKHR DestroySurface;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR GetSurfCaps;
    PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR GetSurfCaps2;
    PFN_vkGetPhysicalDeviceMemoryProperties GetMemProps;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetQueueFamilyProps;
    PFN_vkCreateDevice CreateDevice;
} Inst;

/* Display modes of a portrait display (the swap happens at their reporting),
   and surfaces made on them. */
typedef struct Mode { struct Mode *next; VkDisplayModeKHR mode; } Mode;
typedef struct Surf { struct Surf *next; VkSurfaceKHR surf; VkExtent2D ext; } Surf; /* ext: landscape */

typedef struct Dev {
    struct Dev *next;
    void *key;
    VkDevice device;
    VkPhysicalDevice phys;
    Inst *inst;
    PFN_vkSetDeviceLoaderData SetLoaderData;
    uint32_t gfx_family;   /* a graphics family the program asked for */
    PFN_vkGetDeviceProcAddr gdpa;
#define DF(n) PFN_vk##n n;
    DF(DestroyDevice) DF(CreateSwapchainKHR) DF(DestroySwapchainKHR) DF(GetSwapchainImagesKHR)
    DF(QueuePresentKHR) DF(CreateImage) DF(DestroyImage) DF(GetImageMemoryRequirements)
    DF(AllocateMemory) DF(FreeMemory) DF(BindImageMemory) DF(CreateImageView) DF(DestroyImageView)
    DF(CreateSampler) DF(DestroySampler) DF(CreateRenderPass) DF(DestroyRenderPass)
    DF(CreateFramebuffer) DF(DestroyFramebuffer) DF(CreateShaderModule) DF(DestroyShaderModule)
    DF(CreateDescriptorSetLayout) DF(DestroyDescriptorSetLayout) DF(CreatePipelineLayout)
    DF(DestroyPipelineLayout) DF(CreateGraphicsPipelines) DF(DestroyPipeline)
    DF(CreateDescriptorPool) DF(DestroyDescriptorPool) DF(AllocateDescriptorSets)
    DF(UpdateDescriptorSets) DF(CreateCommandPool) DF(DestroyCommandPool)
    DF(AllocateCommandBuffers) DF(BeginCommandBuffer) DF(EndCommandBuffer)
    DF(CmdPipelineBarrier) DF(CmdBeginRenderPass) DF(CmdEndRenderPass) DF(CmdBindPipeline)
    DF(CmdBindDescriptorSets) DF(CmdPushConstants) DF(CmdDraw) DF(QueueSubmit)
    DF(CreateSemaphore) DF(DestroySemaphore) DF(CreateFence) DF(DestroyFence)
    DF(WaitForFences) DF(ResetFences) DF(DeviceWaitIdle)
#undef DF
} Dev;

typedef struct Swap {
    struct Swap *next;
    VkSwapchainKHR sc;
    Dev *d;
    uint32_t n;
    VkExtent2D app_ext, real_ext;
    VkImage real[MAX_IMAGES], app[MAX_IMAGES];
    VkDeviceMemory mem[MAX_IMAGES];
    VkImageView app_view[MAX_IMAGES], real_view[MAX_IMAGES];
    VkFramebuffer fb[MAX_IMAGES];
    VkCommandBuffer cb[MAX_IMAGES];
    VkFence fence[MAX_IMAGES];
    VkSemaphore done[MAX_IMAGES];
    VkDescriptorSet ds[MAX_IMAGES];
    VkRenderPass rp;
    VkPipeline pipe;
    VkPipelineLayout pl;
    VkDescriptorSetLayout dsl;
    VkDescriptorPool dp;
    VkSampler samp;
    VkCommandPool pool;
} Swap;

static Inst *g_insts;
static Mode *g_modes;
static Surf *g_surfs;
static Dev *g_devs;
static Swap *g_swaps;

static Inst *inst_of(const void *h)
{
    void *k = dkey(h);
    pthread_mutex_lock(&g_lock);
    Inst *i = g_insts;
    while (i && i->key != k) i = i->next;
    pthread_mutex_unlock(&g_lock);
    return i;
}

static Dev *dev_of(const void *h)
{
    void *k = dkey(h);
    pthread_mutex_lock(&g_lock);
    Dev *d = g_devs;
    while (d && d->key != k) d = d->next;
    pthread_mutex_unlock(&g_lock);
    return d;
}

static int mode_rotated(VkDisplayModeKHR m)
{
    pthread_mutex_lock(&g_lock);
    Mode *x = g_modes;
    while (x && x->mode != m) x = x->next;
    pthread_mutex_unlock(&g_lock);
    return x != NULL;
}

static void mode_add(VkDisplayModeKHR m)
{
    if (mode_rotated(m))
        return;
    Mode *x = calloc(1, sizeof(*x));
    if (!x) return;
    x->mode = m;
    pthread_mutex_lock(&g_lock);
    x->next = g_modes; g_modes = x;
    pthread_mutex_unlock(&g_lock);
}

static Surf *surf_find(VkSurfaceKHR s)
{
    pthread_mutex_lock(&g_lock);
    Surf *x = g_surfs;
    while (x && x->surf != s) x = x->next;
    pthread_mutex_unlock(&g_lock);
    return x;
}

static Swap *swap_find(VkSwapchainKHR sc)
{
    pthread_mutex_lock(&g_lock);
    Swap *x = g_swaps;
    while (x && x->sc != sc) x = x->next;
    pthread_mutex_unlock(&g_lock);
    return x;
}

static void swap_ext(VkExtent2D *e) { uint32_t t = e->width; e->width = e->height; e->height = t; }
static void swap_off(VkOffset2D *o) { int32_t t = o->x; o->x = o->y; o->y = t; }

/* ------------------------------------------------------------------------ */
/* Instance level */

static VKAPI_ATTR VkResult VKAPI_CALL L_CreateInstance(const VkInstanceCreateInfo *ci,
        const VkAllocationCallbacks *alloc, VkInstance *out)
{
    init_env();
    VkLayerInstanceCreateInfo *lci = (VkLayerInstanceCreateInfo *)ci->pNext;
    while (lci && !(lci->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO &&
                    lci->function == VK_LAYER_LINK_INFO))
        lci = (VkLayerInstanceCreateInfo *)lci->pNext;
    if (!lci)
        return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr gipa = lci->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    lci->u.pLayerInfo = lci->u.pLayerInfo->pNext;
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)gipa(VK_NULL_HANDLE, "vkCreateInstance");
    VkResult r = create(ci, alloc, out);
    if (r != VK_SUCCESS)
        return r;

    Inst *i = calloc(1, sizeof(*i));
    if (!i) return VK_SUCCESS;
    i->key = dkey(*out);
    i->instance = *out;
    i->gipa = gipa;
#define IF(field, name) i->field = (void *)gipa(*out, name)
    IF(DestroyInstance, "vkDestroyInstance");
    IF(GetDisplayProps, "vkGetPhysicalDeviceDisplayPropertiesKHR");
    IF(GetDisplayProps2, "vkGetPhysicalDeviceDisplayProperties2KHR");
    IF(GetModeProps, "vkGetDisplayModePropertiesKHR");
    IF(GetModeProps2, "vkGetDisplayModeProperties2KHR");
    IF(CreateDisplayMode, "vkCreateDisplayModeKHR");
    IF(GetPlaneCaps, "vkGetDisplayPlaneCapabilitiesKHR");
    IF(GetPlaneCaps2, "vkGetDisplayPlaneCapabilities2KHR");
    IF(CreateDisplayPlaneSurface, "vkCreateDisplayPlaneSurfaceKHR");
    IF(DestroySurface, "vkDestroySurfaceKHR");
    IF(GetSurfCaps, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    IF(GetSurfCaps2, "vkGetPhysicalDeviceSurfaceCapabilities2KHR");
    IF(GetMemProps, "vkGetPhysicalDeviceMemoryProperties");
    IF(GetQueueFamilyProps, "vkGetPhysicalDeviceQueueFamilyProperties");
    IF(CreateDevice, "vkCreateDevice");
#undef IF
    pthread_mutex_lock(&g_lock);
    i->next = g_insts; g_insts = i;
    pthread_mutex_unlock(&g_lock);
    LOG("instance created, turning %d degrees", g_rot);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL L_DestroyInstance(VkInstance instance, const VkAllocationCallbacks *alloc)
{
    Inst *i = inst_of(instance);
    if (!i) return;
    PFN_vkDestroyInstance destroy = i->DestroyInstance;
    pthread_mutex_lock(&g_lock);
    Inst **pp = &g_insts;
    while (*pp && *pp != i) pp = &(*pp)->next;
    if (*pp) *pp = i->next;
    pthread_mutex_unlock(&g_lock);
    free(i);
    destroy(instance, alloc);
}

static int portrait(VkExtent2D e) { return e.height > e.width; }

static void fix_display_props(VkDisplayPropertiesKHR *p)
{
    if (portrait(p->physicalResolution)) {
        swap_ext(&p->physicalResolution);
        swap_ext(&p->physicalDimensions);
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL L_GetDisplayProps(VkPhysicalDevice pd, uint32_t *count, VkDisplayPropertiesKHR *props)
{
    Inst *i = inst_of(pd);
    VkResult r = i->GetDisplayProps(pd, count, props);
    if (props && (r == VK_SUCCESS || r == VK_INCOMPLETE))
        for (uint32_t k = 0; k < *count; k++) {
            if (portrait(props[k].physicalResolution))
                LOG("display %ux%u reported as landscape", props[k].physicalResolution.width, props[k].physicalResolution.height);
            fix_display_props(&props[k]);
        }
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL L_GetDisplayProps2(VkPhysicalDevice pd, uint32_t *count, VkDisplayProperties2KHR *props)
{
    Inst *i = inst_of(pd);
    VkResult r = i->GetDisplayProps2(pd, count, props);
    if (props && (r == VK_SUCCESS || r == VK_INCOMPLETE))
        for (uint32_t k = 0; k < *count; k++)
            fix_display_props(&props[k].displayProperties);
    return r;
}

/* A mode is rotated when its visible region is portrait. Its handle is
   remembered so that its plane capabilities and the surfaces made on it are
   treated the same way. */
static void fix_mode(VkDisplayModePropertiesKHR *m)
{
    if (portrait(m->parameters.visibleRegion)) {
        swap_ext(&m->parameters.visibleRegion);
        mode_add(m->displayMode);
        LOG("mode %p reported as %ux%u", (void *)m->displayMode,
            m->parameters.visibleRegion.width, m->parameters.visibleRegion.height);
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL L_GetModeProps(VkPhysicalDevice pd, VkDisplayKHR disp, uint32_t *count, VkDisplayModePropertiesKHR *props)
{
    Inst *i = inst_of(pd);
    VkResult r = i->GetModeProps(pd, disp, count, props);
    if (props && (r == VK_SUCCESS || r == VK_INCOMPLETE))
        for (uint32_t k = 0; k < *count; k++)
            fix_mode(&props[k]);
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL L_GetModeProps2(VkPhysicalDevice pd, VkDisplayKHR disp, uint32_t *count, VkDisplayModeProperties2KHR *props)
{
    Inst *i = inst_of(pd);
    VkResult r = i->GetModeProps2(pd, disp, count, props);
    if (props && (r == VK_SUCCESS || r == VK_INCOMPLETE))
        for (uint32_t k = 0; k < *count; k++)
            fix_mode(&props[k].displayModeProperties);
    return r;
}

/* Asking for a landscape mode on a portrait display: ask for the portrait one
   underneath. Whether the display is portrait comes from its properties. */
static int display_portrait(Inst *i, VkPhysicalDevice pd, VkDisplayKHR disp)
{
    uint32_t n = 0;
    if (!i->GetDisplayProps || i->GetDisplayProps(pd, &n, NULL) != VK_SUCCESS || !n || n > 16)
        return 0;
    VkDisplayPropertiesKHR p[16];
    if (i->GetDisplayProps(pd, &n, p) != VK_SUCCESS && n == 0)
        return 0;
    for (uint32_t k = 0; k < n; k++)
        if (p[k].display == disp)
            return portrait(p[k].physicalResolution);
    return 0;
}

static VKAPI_ATTR VkResult VKAPI_CALL L_CreateDisplayMode(VkPhysicalDevice pd, VkDisplayKHR disp,
        const VkDisplayModeCreateInfoKHR *ci, const VkAllocationCallbacks *alloc, VkDisplayModeKHR *out)
{
    Inst *i = inst_of(pd);
    if (display_portrait(i, pd, disp) && !portrait(ci->parameters.visibleRegion)) {
        VkDisplayModeCreateInfoKHR c = *ci;
        swap_ext(&c.parameters.visibleRegion);
        VkResult r = i->CreateDisplayMode(pd, disp, &c, alloc, out);
        if (r == VK_SUCCESS)
            mode_add(*out);
        LOG("display mode %ux%u created portrait: %d", ci->parameters.visibleRegion.width,
            ci->parameters.visibleRegion.height, r);
        return r;
    }
    return i->CreateDisplayMode(pd, disp, ci, alloc, out);
}

static void fix_plane_caps(VkDisplayPlaneCapabilitiesKHR *c)
{
    swap_off(&c->minSrcPosition); swap_off(&c->maxSrcPosition);
    swap_ext(&c->minSrcExtent);   swap_ext(&c->maxSrcExtent);
    swap_off(&c->minDstPosition); swap_off(&c->maxDstPosition);
    swap_ext(&c->minDstExtent);   swap_ext(&c->maxDstExtent);
}

static VKAPI_ATTR VkResult VKAPI_CALL L_GetPlaneCaps(VkPhysicalDevice pd, VkDisplayModeKHR mode, uint32_t plane, VkDisplayPlaneCapabilitiesKHR *caps)
{
    Inst *i = inst_of(pd);
    VkResult r = i->GetPlaneCaps(pd, mode, plane, caps);
    if (r == VK_SUCCESS && mode_rotated(mode))
        fix_plane_caps(caps);
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL L_GetPlaneCaps2(VkPhysicalDevice pd, const VkDisplayPlaneInfo2KHR *info, VkDisplayPlaneCapabilities2KHR *caps)
{
    Inst *i = inst_of(pd);
    VkResult r = i->GetPlaneCaps2(pd, info, caps);
    if (r == VK_SUCCESS && mode_rotated(info->mode))
        fix_plane_caps(&caps->capabilities);
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL L_CreateDisplayPlaneSurface(VkInstance instance,
        const VkDisplaySurfaceCreateInfoKHR *ci, const VkAllocationCallbacks *alloc, VkSurfaceKHR *out)
{
    Inst *i = inst_of(instance);
    if (!mode_rotated(ci->displayMode) || ci->transform != VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
        return i->CreateDisplayPlaneSurface(instance, ci, alloc, out);

    VkDisplaySurfaceCreateInfoKHR c = *ci;
    VkExtent2D land = ci->imageExtent;
    if (!portrait(land))
        swap_ext(&c.imageExtent);      /* the program asked for landscape */
    else
        swap_ext(&land);               /* it asked for the panel's own size */
    VkResult r = i->CreateDisplayPlaneSurface(instance, &c, alloc, out);
    if (r == VK_SUCCESS) {
        Surf *s = calloc(1, sizeof(*s));
        if (s) {
            s->surf = *out;
            s->ext = land;
            pthread_mutex_lock(&g_lock);
            s->next = g_surfs; g_surfs = s;
            pthread_mutex_unlock(&g_lock);
        }
    }
    LOG("display surface %ux%u (panel %ux%u): %d", land.width, land.height,
        c.imageExtent.width, c.imageExtent.height, r);
    return r;
}

static VKAPI_ATTR void VKAPI_CALL L_DestroySurface(VkInstance instance, VkSurfaceKHR surf, const VkAllocationCallbacks *alloc)
{
    Inst *i = inst_of(instance);
    pthread_mutex_lock(&g_lock);
    Surf **pp = &g_surfs;
    while (*pp && (*pp)->surf != surf) pp = &(*pp)->next;
    if (*pp) { Surf *s = *pp; *pp = s->next; free(s); }
    pthread_mutex_unlock(&g_lock);
    i->DestroySurface(instance, surf, alloc);
}

static void fix_surf_caps(VkSurfaceCapabilitiesKHR *c)
{
    if (c->currentExtent.width != 0xFFFFFFFFu)
        swap_ext(&c->currentExtent);
    swap_ext(&c->minImageExtent);
    swap_ext(&c->maxImageExtent);
}

static VKAPI_ATTR VkResult VKAPI_CALL L_GetSurfCaps(VkPhysicalDevice pd, VkSurfaceKHR surf, VkSurfaceCapabilitiesKHR *caps)
{
    Inst *i = inst_of(pd);
    VkResult r = i->GetSurfCaps(pd, surf, caps);
    if (r == VK_SUCCESS && surf_find(surf))
        fix_surf_caps(caps);
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL L_GetSurfCaps2(VkPhysicalDevice pd, const VkPhysicalDeviceSurfaceInfo2KHR *info, VkSurfaceCapabilities2KHR *caps)
{
    Inst *i = inst_of(pd);
    VkResult r = i->GetSurfCaps2(pd, info, caps);
    if (r == VK_SUCCESS && surf_find(info->surface))
        fix_surf_caps(&caps->surfaceCapabilities);
    return r;
}

/* ------------------------------------------------------------------------ */
/* Device level */

static VKAPI_ATTR VkResult VKAPI_CALL L_CreateDevice(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci,
        const VkAllocationCallbacks *alloc, VkDevice *out)
{
    Inst *i = inst_of(pd);
    VkLayerDeviceCreateInfo *link = (VkLayerDeviceCreateInfo *)ci->pNext;
    while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
                     link->function == VK_LAYER_LINK_INFO))
        link = (VkLayerDeviceCreateInfo *)link->pNext;
    if (!link)
        return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr gdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;

    VkLayerDeviceCreateInfo *cb = (VkLayerDeviceCreateInfo *)ci->pNext;
    while (cb && !(cb->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO &&
                   cb->function == VK_LOADER_DATA_CALLBACK))
        cb = (VkLayerDeviceCreateInfo *)cb->pNext;

    PFN_vkCreateDevice create = (PFN_vkCreateDevice)gipa(i->instance, "vkCreateDevice");
    VkResult r = create(pd, ci, alloc, out);
    if (r != VK_SUCCESS)
        return r;

    Dev *d = calloc(1, sizeof(*d));
    if (!d) return VK_SUCCESS;
    d->key = dkey(*out);
    d->device = *out;
    d->phys = pd;
    d->inst = i;
    d->gdpa = gdpa;
    d->SetLoaderData = cb ? cb->u.pfnSetDeviceLoaderData : NULL;

    /* the queue the rotation pass goes to: the first graphics family the
       program created queues in (presenting from it is what it does) */
    d->gfx_family = UINT32_MAX;
    uint32_t nf = 0;
    i->GetQueueFamilyProps(pd, &nf, NULL);
    VkQueueFamilyProperties qf[16];
    if (nf > 16) nf = 16;
    i->GetQueueFamilyProps(pd, &nf, qf);
    for (uint32_t k = 0; k < ci->queueCreateInfoCount && d->gfx_family == UINT32_MAX; k++) {
        uint32_t f = ci->pQueueCreateInfos[k].queueFamilyIndex;
        if (f < nf && (qf[f].queueFlags & VK_QUEUE_GRAPHICS_BIT))
            d->gfx_family = f;
    }

#define DF(n) d->n = (PFN_vk##n)gdpa(*out, "vk" #n);
    DF(DestroyDevice) DF(CreateSwapchainKHR) DF(DestroySwapchainKHR) DF(GetSwapchainImagesKHR)
    DF(QueuePresentKHR) DF(CreateImage) DF(DestroyImage) DF(GetImageMemoryRequirements)
    DF(AllocateMemory) DF(FreeMemory) DF(BindImageMemory) DF(CreateImageView) DF(DestroyImageView)
    DF(CreateSampler) DF(DestroySampler) DF(CreateRenderPass) DF(DestroyRenderPass)
    DF(CreateFramebuffer) DF(DestroyFramebuffer) DF(CreateShaderModule) DF(DestroyShaderModule)
    DF(CreateDescriptorSetLayout) DF(DestroyDescriptorSetLayout) DF(CreatePipelineLayout)
    DF(DestroyPipelineLayout) DF(CreateGraphicsPipelines) DF(DestroyPipeline)
    DF(CreateDescriptorPool) DF(DestroyDescriptorPool) DF(AllocateDescriptorSets)
    DF(UpdateDescriptorSets) DF(CreateCommandPool) DF(DestroyCommandPool)
    DF(AllocateCommandBuffers) DF(BeginCommandBuffer) DF(EndCommandBuffer)
    DF(CmdPipelineBarrier) DF(CmdBeginRenderPass) DF(CmdEndRenderPass) DF(CmdBindPipeline)
    DF(CmdBindDescriptorSets) DF(CmdPushConstants) DF(CmdDraw) DF(QueueSubmit)
    DF(CreateSemaphore) DF(DestroySemaphore) DF(CreateFence) DF(DestroyFence)
    DF(WaitForFences) DF(ResetFences) DF(DeviceWaitIdle)
#undef DF
    pthread_mutex_lock(&g_lock);
    d->next = g_devs; g_devs = d;
    pthread_mutex_unlock(&g_lock);
    LOG("device created, graphics family %u", d->gfx_family);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL L_DestroyDevice(VkDevice device, const VkAllocationCallbacks *alloc)
{
    Dev *d = dev_of(device);
    if (!d) return;
    PFN_vkDestroyDevice destroy = d->DestroyDevice;
    pthread_mutex_lock(&g_lock);
    Dev **pp = &g_devs;
    while (*pp && *pp != d) pp = &(*pp)->next;
    if (*pp) *pp = d->next;
    pthread_mutex_unlock(&g_lock);
    free(d);
    destroy(device, alloc);
}

static uint32_t find_memory(Dev *d, uint32_t bits, VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties mp;
    d->inst->GetMemProps(d->phys, &mp);
    for (uint32_t k = 0; k < mp.memoryTypeCount; k++)
        if ((bits & (1u << k)) && (mp.memoryTypes[k].propertyFlags & want) == want)
            return k;
    for (uint32_t k = 0; k < mp.memoryTypeCount; k++)
        if (bits & (1u << k))
            return k;
    return 0;
}

static void swap_free(Swap *s)
{
    Dev *d = s->d;
    VkDevice dv = d->device;
    for (uint32_t k = 0; k < s->n; k++) {
        if (s->fb[k]) d->DestroyFramebuffer(dv, s->fb[k], NULL);
        if (s->real_view[k]) d->DestroyImageView(dv, s->real_view[k], NULL);
        if (s->app_view[k]) d->DestroyImageView(dv, s->app_view[k], NULL);
        if (s->app[k]) d->DestroyImage(dv, s->app[k], NULL);
        if (s->mem[k]) d->FreeMemory(dv, s->mem[k], NULL);
        if (s->fence[k]) d->DestroyFence(dv, s->fence[k], NULL);
        if (s->done[k]) d->DestroySemaphore(dv, s->done[k], NULL);
    }
    if (s->pool) d->DestroyCommandPool(dv, s->pool, NULL);
    if (s->pipe) d->DestroyPipeline(dv, s->pipe, NULL);
    if (s->pl) d->DestroyPipelineLayout(dv, s->pl, NULL);
    if (s->dp) d->DestroyDescriptorPool(dv, s->dp, NULL);
    if (s->dsl) d->DestroyDescriptorSetLayout(dv, s->dsl, NULL);
    if (s->samp) d->DestroySampler(dv, s->samp, NULL);
    if (s->rp) d->DestroyRenderPass(dv, s->rp, NULL);
    free(s);
}

#define CHECK(x) do { VkResult _r = (x); if (_r != VK_SUCCESS) { LOG("%s failed: %d", #x, _r); goto fail; } } while (0)

/* Everything the rotation pass needs for one swapchain, with one command
   buffer per image recorded once. */
static int swap_build(Swap *s, const VkSwapchainCreateInfoKHR *ci)
{
    Dev *d = s->d;
    VkDevice dv = d->device;
    VkFormat fmt = ci->imageFormat;

    /* render pass: the swapchain image, written whole, left for presenting */
    VkAttachmentDescription att = {
        .format = fmt, .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };
    VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkSubpassDescription sub = { .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1, .pColorAttachments = &ref };
    VkSubpassDependency dep = {
        .srcSubpass = VK_SUBPASS_EXTERNAL, .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0, .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    VkRenderPassCreateInfo rpi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &att, .subpassCount = 1, .pSubpasses = &sub,
        .dependencyCount = 1, .pDependencies = &dep };
    CHECK(d->CreateRenderPass(dv, &rpi, NULL, &s->rp));

    VkSamplerCreateInfo sci = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST, .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .maxLod = 0.0f };
    CHECK(d->CreateSampler(dv, &sci, NULL, &s->samp));

    VkDescriptorSetLayoutBinding b = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
        VK_SHADER_STAGE_FRAGMENT_BIT, &s->samp };
    VkDescriptorSetLayoutCreateInfo dli = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &b };
    CHECK(d->CreateDescriptorSetLayout(dv, &dli, NULL, &s->dsl));

    VkPushConstantRange pc = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(int32_t) };
    VkPipelineLayoutCreateInfo pli = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &s->dsl, .pushConstantRangeCount = 1, .pPushConstantRanges = &pc };
    CHECK(d->CreatePipelineLayout(dv, &pli, NULL, &s->pl));

    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo smi = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = sizeof(rotate_vert_spv), .pCode = rotate_vert_spv };
    CHECK(d->CreateShaderModule(dv, &smi, NULL, &vs));
    smi.codeSize = sizeof(rotate_frag_spv);
    smi.pCode = rotate_frag_spv;
    if (d->CreateShaderModule(dv, &smi, NULL, &fs) != VK_SUCCESS) {
        d->DestroyShaderModule(dv, vs, NULL);
        goto fail;
    }
    VkPipelineShaderStageCreateInfo st[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main" },
    };
    VkPipelineVertexInputStateCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkViewport vp = { 0, 0, (float)s->real_ext.width, (float)s->real_ext.height, 0, 1 };
    VkRect2D sc = { { 0, 0 }, s->real_ext };
    VkPipelineViewportStateCreateInfo vps = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc };
    VkPipelineRasterizationStateCreateInfo rs = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo ms = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState cba = { .colorWriteMask = 0xF };
    VkPipelineColorBlendStateCreateInfo cb = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &cba };
    VkGraphicsPipelineCreateInfo gpi = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2, .pStages = st, .pVertexInputState = &vi, .pInputAssemblyState = &ia,
        .pViewportState = &vps, .pRasterizationState = &rs, .pMultisampleState = &ms,
        .pColorBlendState = &cb, .layout = s->pl, .renderPass = s->rp, .subpass = 0 };
    VkResult pr = d->CreateGraphicsPipelines(dv, VK_NULL_HANDLE, 1, &gpi, NULL, &s->pipe);
    d->DestroyShaderModule(dv, vs, NULL);
    d->DestroyShaderModule(dv, fs, NULL);
    CHECK(pr);

    VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, s->n };
    VkDescriptorPoolCreateInfo dpi = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = s->n, .poolSizeCount = 1, .pPoolSizes = &ps };
    CHECK(d->CreateDescriptorPool(dv, &dpi, NULL, &s->dp));

    VkCommandPoolCreateInfo cpi = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = d->gfx_family };
    CHECK(d->CreateCommandPool(dv, &cpi, NULL, &s->pool));
    VkCommandBufferAllocateInfo cai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = s->pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = s->n };
    CHECK(d->AllocateCommandBuffers(dv, &cai, s->cb));

    for (uint32_t k = 0; k < s->n; k++) {
        if (d->SetLoaderData)
            d->SetLoaderData(dv, s->cb[k]);

        /* the program's image: landscape, its usage plus sampling */
        VkImageCreateInfo ii = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D, .format = fmt,
            .extent = { s->app_ext.width, s->app_ext.height, 1 }, .mipLevels = 1,
            .arrayLayers = ci->imageArrayLayers, .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = ci->imageUsage | VK_IMAGE_USAGE_SAMPLED_BIT,
            .sharingMode = ci->imageSharingMode,
            .queueFamilyIndexCount = ci->imageSharingMode == VK_SHARING_MODE_CONCURRENT ? ci->queueFamilyIndexCount : 0,
            .pQueueFamilyIndices = ci->imageSharingMode == VK_SHARING_MODE_CONCURRENT ? ci->pQueueFamilyIndices : NULL,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
        CHECK(d->CreateImage(dv, &ii, NULL, &s->app[k]));
        VkMemoryRequirements mr;
        d->GetImageMemoryRequirements(dv, s->app[k], &mr);
        VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = mr.size,
            .memoryTypeIndex = find_memory(d, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) };
        CHECK(d->AllocateMemory(dv, &mai, NULL, &s->mem[k]));
        CHECK(d->BindImageMemory(dv, s->app[k], s->mem[k], 0));

        VkImageViewCreateInfo vci = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = s->app[k], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        CHECK(d->CreateImageView(dv, &vci, NULL, &s->app_view[k]));
        vci.image = s->real[k];
        CHECK(d->CreateImageView(dv, &vci, NULL, &s->real_view[k]));

        VkFramebufferCreateInfo fbi = { .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = s->rp, .attachmentCount = 1, .pAttachments = &s->real_view[k],
            .width = s->real_ext.width, .height = s->real_ext.height, .layers = 1 };
        CHECK(d->CreateFramebuffer(dv, &fbi, NULL, &s->fb[k]));

        VkDescriptorSetAllocateInfo dai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = s->dp, .descriptorSetCount = 1, .pSetLayouts = &s->dsl };
        CHECK(d->AllocateDescriptorSets(dv, &dai, &s->ds[k]));
        VkDescriptorImageInfo dii = { s->samp, s->app_view[k], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
        VkWriteDescriptorSet w = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = s->ds[k],
            .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &dii };
        d->UpdateDescriptorSets(dv, 1, &w, 0, NULL);

        VkFenceCreateInfo fi = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT };
        CHECK(d->CreateFence(dv, &fi, NULL, &s->fence[k]));
        VkSemaphoreCreateInfo smci = { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        CHECK(d->CreateSemaphore(dv, &smci, NULL, &s->done[k]));

        /* the pass: the program's image (in PRESENT_SRC, as it presented
           it) is read turned into the swapchain image, then handed back in
           PRESENT_SRC so that the program finds it as it left it */
        VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        CHECK(d->BeginCommandBuffer(s->cb[k], &bi));
        VkImageMemoryBarrier ib = { .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT, .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = s->app[k], .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        d->CmdPipelineBarrier(s->cb[k], VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, NULL, 0, NULL, 1, &ib);
        VkRenderPassBeginInfo rbi = { .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = s->rp, .framebuffer = s->fb[k], .renderArea = { { 0, 0 }, s->real_ext } };
        d->CmdBeginRenderPass(s->cb[k], &rbi, VK_SUBPASS_CONTENTS_INLINE);
        d->CmdBindPipeline(s->cb[k], VK_PIPELINE_BIND_POINT_GRAPHICS, s->pipe);
        d->CmdBindDescriptorSets(s->cb[k], VK_PIPELINE_BIND_POINT_GRAPHICS, s->pl, 0, 1, &s->ds[k], 0, NULL);
        int32_t rot = g_rot;
        d->CmdPushConstants(s->cb[k], s->pl, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(rot), &rot);
        d->CmdDraw(s->cb[k], 3, 1, 0, 0);
        d->CmdEndRenderPass(s->cb[k]);
        ib.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        ib.dstAccessMask = 0;
        ib.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ib.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        d->CmdPipelineBarrier(s->cb[k], VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, NULL, 0, NULL, 1, &ib);
        CHECK(d->EndCommandBuffer(s->cb[k]));
    }
    return 1;
fail:
    return 0;
}

static VKAPI_ATTR VkResult VKAPI_CALL L_CreateSwapchain(VkDevice device, const VkSwapchainCreateInfoKHR *ci,
        const VkAllocationCallbacks *alloc, VkSwapchainKHR *out)
{
    Dev *d = dev_of(device);
    Surf *sf = surf_find(ci->surface);
    if (!sf || d->gfx_family == UINT32_MAX)
        return d->CreateSwapchainKHR(device, ci, alloc, out);

    VkSwapchainCreateInfoKHR c = *ci;
    VkExtent2D app = ci->imageExtent;
    c.imageExtent.width = app.height;
    c.imageExtent.height = app.width;
    c.imageUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    c.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    VkResult r = d->CreateSwapchainKHR(device, &c, alloc, out);
    if (r != VK_SUCCESS) {
        LOG("swapchain %ux%u failed: %d", c.imageExtent.width, c.imageExtent.height, r);
        return r;
    }

    Swap *s = calloc(1, sizeof(*s));
    if (!s) goto plain;
    s->sc = *out;
    s->d = d;
    s->app_ext = app;
    s->real_ext = c.imageExtent;
    uint32_t n = 0;
    d->GetSwapchainImagesKHR(device, *out, &n, NULL);
    if (n == 0 || n > MAX_IMAGES) { free(s); goto plain; }
    s->n = n;
    d->GetSwapchainImagesKHR(device, *out, &n, s->real);
    if (!swap_build(s, ci)) {
        d->DeviceWaitIdle(device);
        swap_free(s);
        goto plain;
    }
    pthread_mutex_lock(&g_lock);
    s->next = g_swaps; g_swaps = s;
    pthread_mutex_unlock(&g_lock);
    LOG("swapchain: program %ux%u, panel %ux%u, %u images", app.width, app.height,
        s->real_ext.width, s->real_ext.height, n);
    return VK_SUCCESS;

plain:
    /* could not set up the pass: give the program the panel as it is rather
       than a broken swapchain */
    d->DestroySwapchainKHR(device, *out, alloc);
    LOG("rotation unavailable, plain swapchain");
    return d->CreateSwapchainKHR(device, ci, alloc, out);
}

static VKAPI_ATTR void VKAPI_CALL L_DestroySwapchain(VkDevice device, VkSwapchainKHR sc, const VkAllocationCallbacks *alloc)
{
    Dev *d = dev_of(device);
    pthread_mutex_lock(&g_lock);
    Swap **pp = &g_swaps;
    while (*pp && (*pp)->sc != sc) pp = &(*pp)->next;
    Swap *s = *pp;
    if (s) *pp = s->next;
    pthread_mutex_unlock(&g_lock);
    if (s) {
        d->WaitForFences(device, s->n, s->fence, VK_TRUE, UINT64_MAX);
        swap_free(s);
    }
    d->DestroySwapchainKHR(device, sc, alloc);
}

static VKAPI_ATTR VkResult VKAPI_CALL L_GetSwapchainImages(VkDevice device, VkSwapchainKHR sc, uint32_t *count, VkImage *images)
{
    Dev *d = dev_of(device);
    Swap *s = swap_find(sc);
    if (!s)
        return d->GetSwapchainImagesKHR(device, sc, count, images);
    if (!images) { *count = s->n; return VK_SUCCESS; }
    uint32_t n = *count < s->n ? *count : s->n;
    memcpy(images, s->app, n * sizeof(VkImage));
    *count = n;
    return n < s->n ? VK_INCOMPLETE : VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL L_QueuePresent(VkQueue queue, const VkPresentInfoKHR *pi)
{
    Dev *d = dev_of(queue);
    Swap *rot = NULL;
    uint32_t idx = 0;
    for (uint32_t k = 0; k < pi->swapchainCount && !rot; k++) {
        Swap *s = swap_find(pi->pSwapchains[k]);
        if (s) { rot = s; idx = pi->pImageIndices[k]; }
    }
    if (!rot || idx >= rot->n)
        return d->QueuePresentKHR(queue, pi);

    /* the pass waits for what the program said its frame waits for, and the
       present waits for the pass */
    d->WaitForFences(d->device, 1, &rot->fence[idx], VK_TRUE, UINT64_MAX);
    d->ResetFences(d->device, 1, &rot->fence[idx]);
    VkPipelineStageFlags stages[16];
    uint32_t nw = pi->waitSemaphoreCount < 16 ? pi->waitSemaphoreCount : 16;
    for (uint32_t k = 0; k < nw; k++)
        stages[k] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = nw, .pWaitSemaphores = pi->pWaitSemaphores, .pWaitDstStageMask = stages,
        .commandBufferCount = 1, .pCommandBuffers = &rot->cb[idx],
        .signalSemaphoreCount = 1, .pSignalSemaphores = &rot->done[idx] };
    VkResult r = d->QueueSubmit(queue, 1, &si, rot->fence[idx]);
    if (r != VK_SUCCESS) {
        LOG("rotation submit failed: %d", r);
        return r;
    }
    VkPresentInfoKHR p = *pi;
    p.waitSemaphoreCount = 1;
    p.pWaitSemaphores = &rot->done[idx];
    return d->QueuePresentKHR(queue, &p);
}

/* ------------------------------------------------------------------------ */
/* Entry points */

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL L_GetDeviceProcAddr(VkDevice device, const char *name);

#define HOOK(n, f) if (!strcmp(name, n)) return (PFN_vkVoidFunction)f

static PFN_vkVoidFunction device_hook(const char *name)
{
    HOOK("vkGetDeviceProcAddr", L_GetDeviceProcAddr);
    HOOK("vkDestroyDevice", L_DestroyDevice);
    HOOK("vkCreateSwapchainKHR", L_CreateSwapchain);
    HOOK("vkDestroySwapchainKHR", L_DestroySwapchain);
    HOOK("vkGetSwapchainImagesKHR", L_GetSwapchainImages);
    HOOK("vkQueuePresentKHR", L_QueuePresent);
    return NULL;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL L_GetDeviceProcAddr(VkDevice device, const char *name)
{
    Dev *d = dev_of(device);
    PFN_vkVoidFunction next = d ? d->gdpa(device, name) : NULL;
    PFN_vkVoidFunction mine = device_hook(name);
    return (mine && next) ? mine : next;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL L_GetInstanceProcAddr(VkInstance instance, const char *name)
{
    HOOK("vkGetInstanceProcAddr", L_GetInstanceProcAddr);
    HOOK("vkCreateInstance", L_CreateInstance);
    if (!instance)
        return NULL;
    Inst *i = inst_of(instance);
    if (!i)
        return NULL;
    PFN_vkVoidFunction next = i->gipa(instance, name);
    if (!next)
        return NULL;
    HOOK("vkDestroyInstance", L_DestroyInstance);
    HOOK("vkCreateDevice", L_CreateDevice);
    HOOK("vkGetPhysicalDeviceDisplayPropertiesKHR", L_GetDisplayProps);
    HOOK("vkGetPhysicalDeviceDisplayProperties2KHR", L_GetDisplayProps2);
    HOOK("vkGetDisplayModePropertiesKHR", L_GetModeProps);
    HOOK("vkGetDisplayModeProperties2KHR", L_GetModeProps2);
    HOOK("vkCreateDisplayModeKHR", L_CreateDisplayMode);
    HOOK("vkGetDisplayPlaneCapabilitiesKHR", L_GetPlaneCaps);
    HOOK("vkGetDisplayPlaneCapabilities2KHR", L_GetPlaneCaps2);
    HOOK("vkCreateDisplayPlaneSurfaceKHR", L_CreateDisplayPlaneSurface);
    HOOK("vkDestroySurfaceKHR", L_DestroySurface);
    HOOK("vkGetPhysicalDeviceSurfaceCapabilitiesKHR", L_GetSurfCaps);
    HOOK("vkGetPhysicalDeviceSurfaceCapabilities2KHR", L_GetSurfCaps2);
    PFN_vkVoidFunction dev = device_hook(name);
    if (dev)
        return dev;
    return next;
}

EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface *v)
{
    if (v->loaderLayerInterfaceVersion > 2)
        v->loaderLayerInterfaceVersion = 2;
    v->pfnGetInstanceProcAddr = L_GetInstanceProcAddr;
    v->pfnGetDeviceProcAddr = L_GetDeviceProcAddr;
    v->pfnGetPhysicalDeviceProcAddr = NULL;
    return VK_SUCCESS;
}
