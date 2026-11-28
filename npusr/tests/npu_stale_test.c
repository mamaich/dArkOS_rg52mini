/*
 * npu_stale_test: does rknpu refuse a submit whose task object is not (or no longer) one of the caller's?
 * For the dArkOS fix of 2026-11-28 (rknpu_task_obj_get). It never runs anything on the NPU: each submit it
 * makes must be refused before a job is made. RUN IT ONLY ON THE FIXED MODULE: rknpu 0.9.8 as shipped took the
 * stale address as it came and crashed the kernel (2026-11-24).
 *   1. a task object address that was never one of this file's (an existing object's address + 64)
 *   2. a task object freed with MEM_DESTROY just before
 * Exit 0 when both submits fail with EINVAL; 1 otherwise.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <drm/drm.h>

struct rknpu_mem_create { uint32_t handle, flags; uint64_t size, obj_addr, dma_addr, sram_size; int32_t iommu_domain_id; uint32_t core_mask; };
struct rknpu_mem_destroy { uint32_t handle, reserved; uint64_t obj_addr; };
struct rknpu_subcore_task { uint32_t task_start, task_number; };
struct rknpu_submit {
    uint32_t flags, timeout, task_start, task_number, task_counter; int32_t priority;
    uint64_t task_obj_addr; uint32_t iommu_domain_id, reserved; uint64_t task_base_addr; int64_t hw_elapse_time;
    uint32_t core_mask; int32_t fence_fd; struct rknpu_subcore_task subcore_task[5];
};
#define IOC_SUBMIT      DRM_IOWR(DRM_COMMAND_BASE + 0x01, struct rknpu_submit)
#define IOC_MEM_CREATE  DRM_IOWR(DRM_COMMAND_BASE + 0x02, struct rknpu_mem_create)
#define IOC_MEM_DESTROY DRM_IOWR(DRM_COMMAND_BASE + 0x04, struct rknpu_mem_destroy)

static int open_rknpu(void)
{
    char path[64], name[64];
    for (int i = 0; i < 8; i++) {
        snprintf(path, sizeof path, i < 4 ? "/dev/dri/renderD%d" : "/dev/dri/card%d", i < 4 ? 128 + i : i - 4);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        struct drm_version v; memset(&v, 0, sizeof v); memset(name, 0, sizeof name);
        v.name = name; v.name_len = sizeof name - 1;
        if (ioctl(fd, DRM_IOCTL_VERSION, &v) == 0 && !strcmp(name, "rknpu")) { printf("rknpu at %s\n", path); return fd; }
        close(fd);
    }
    return -1;
}

static int submit(int fd, uint64_t task_obj_addr)
{
    struct rknpu_submit s; memset(&s, 0, sizeof s);
    s.flags = 1;            /* RKNPU_JOB_PC, blocking: what librknnrt sends */
    s.timeout = 100;
    s.task_number = 1;
    s.task_obj_addr = task_obj_addr;
    s.core_mask = 1;
    s.fence_fd = -1;
    s.subcore_task[0].task_number = 1;
    int r = ioctl(fd, IOC_SUBMIT, &s);
    return r < 0 ? -errno : r;
}

int main(void)
{
    int fd = open_rknpu();
    if (fd < 0) { printf("no rknpu device (module not loaded?)\n"); return 2; }
    struct rknpu_mem_create mc; memset(&mc, 0, sizeof mc);
    mc.flags = (1 << 3);    /* RKNPU_MEM_KERNEL_MAPPING */
    mc.size = 4096;
    mc.core_mask = 1;
    if (ioctl(fd, IOC_MEM_CREATE, &mc) < 0) { printf("MEM_CREATE: %s\n", strerror(errno)); return 2; }
    printf("object: handle %u, obj_addr %#llx\n", mc.handle, (unsigned long long)mc.obj_addr);
    int bad = 0;
    int r = submit(fd, mc.obj_addr + 64);
    printf("1. an address that is no object of this file: %d (%s)\n", r, r == -EINVAL ? "refused, as it should" : "NOT REFUSED");
    if (r != -EINVAL) bad = 1;
    struct rknpu_mem_destroy md = { mc.handle, 0, mc.obj_addr };
    if (ioctl(fd, IOC_MEM_DESTROY, &md) < 0) { printf("MEM_DESTROY: %s\n", strerror(errno)); return 2; }
    r = submit(fd, mc.obj_addr);
    printf("2. the object freed just before: %d (%s)\n", r, r == -EINVAL ? "refused, as it should" : "NOT REFUSED");
    if (r != -EINVAL) bad = 1;
    close(fd);
    return bad;
}
