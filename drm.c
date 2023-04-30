#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <libdrm/drm_fourcc.h>

#include "drm.h"
#include "va.h"

extern struct kmsvnc_data *kmsvnc;

static void convert_copy(const char *in, int width, int height, char *buff) {
    memcpy(buff, in, width * height * 4);
}

static void convert_bgrx_to_rgb(const char *in, int width, int height, char *buff)
{
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            buff[(y * width + x) * 4] = in[(y * width + x) * 4 + 2];
            buff[(y * width + x) * 4 + 1] = in[(y * width + x) * 4 + 1];
            buff[(y * width + x) * 4 + 2] = in[(y * width + x) * 4];
        }
    }
}

static char *kms_convert_buf = NULL;
static size_t kms_convert_buf_len = 0;
static char *kms_cpy_tmp_buf = NULL;
static size_t kms_cpy_tmp_buf_len = 0;
static inline char convert_buf_allocate(size_t len) {
    if (kms_convert_buf_len < len)
    {
        if (kms_convert_buf)
            free(kms_convert_buf);
        kms_convert_buf = malloc(len);
        if (!kms_convert_buf) return 1;
        kms_convert_buf_len = len;
    }
    return 0;
}
static inline void convert_x_tiled(const int tilex, const int tiley, const char *in, int width, int height, char *buff)
{
    if (width % tilex)
    {
        return;
    }
    if (height % tiley)
    {
        int sno = (width / tilex) + (height / tiley) * (width / tilex);
        int ord = (width % tilex) + (height % tiley) * tilex;
        int max_offset = sno * tilex * tiley + ord;
        if (kms_cpy_tmp_buf_len < max_offset * 4 + 4)
        {
            if (kms_cpy_tmp_buf)
                free(kms_convert_buf);
            kms_cpy_tmp_buf = malloc(max_offset * 4 + 4);
            if (!kms_cpy_tmp_buf) return;
            kms_cpy_tmp_buf_len = max_offset * 4 + 4;
        }
        memcpy(kms_cpy_tmp_buf, in, max_offset * 4 + 4);
        in = (const char *)kms_cpy_tmp_buf;
    }
    if (convert_buf_allocate(width * height * 4)) return;
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            int sno = (x / tilex) + (y / tiley) * (width / tilex);
            int ord = (x % tilex) + (y % tiley) * tilex;
            int offset = sno * tilex * tiley + ord;
            memcpy(kms_convert_buf + (x + y * width) * 4, in + offset * 4, 4);
        }
    }
    convert_bgrx_to_rgb(kms_convert_buf, width, height, buff);
}

void convert_nvidia_x_tiled_kmsbuf(const char *in, int width, int height, char *buff)
{
    convert_x_tiled(16, 128, in, width, height, buff);
}
void convert_intel_x_tiled_kmsbuf(const char *in, int width, int height, char *buff)
{
    convert_x_tiled(128, 8, in, width, height, buff);
}

static void convert_vaapi(const char *in, int width, int height, char *buff) {
    if (kmsvnc->va->is_xrgb || kmsvnc->va->is_bgr) {
        if (convert_buf_allocate(width * height * BYTES_PER_PIXEL)) return;
        va_hwframe_to_vaapi(kms_convert_buf);
        if (kmsvnc->va->is_xrgb) {
            for (int i = 0; i < width * height * BYTES_PER_PIXEL; i += BYTES_PER_PIXEL) {
                *((uint32_t*)(kms_convert_buf + i)) <<= 8;
            }
        }
        if (kmsvnc->va->is_bgr) {
            convert_bgrx_to_rgb(kms_convert_buf, width, height, buff);
        }
    }
    else {
        va_hwframe_to_vaapi(buff);
    }
}

static inline void drm_sync(int drmfd, uint64_t flags)
{
    struct dma_buf_sync sync = {
        .flags = flags,
    };
    DRM_R_IOCTL_MAY(drmfd, DMA_BUF_IOCTL_SYNC, &sync);
}

void drm_sync_start(int drmfd)
{
    drm_sync(drmfd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
}
void drm_sync_end(int drmfd)
{
    drm_sync(drmfd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
}
void drm_sync_noop(int drmfd)
{
}

void drm_cleanup() {
    if (kmsvnc->drm) {
        if (kmsvnc->drm->drm_ver) {
            drmFreeVersion(kmsvnc->drm->drm_ver);
            kmsvnc->drm->drm_ver = NULL;
        }
        if (kmsvnc->drm->pixfmt_name) {
            free(kmsvnc->drm->pixfmt_name);
            kmsvnc->drm->pixfmt_name = NULL;
        }
        if (kmsvnc->drm->mod_vendor) {
            free(kmsvnc->drm->mod_vendor);
            kmsvnc->drm->mod_vendor = NULL;
        }
        if (kmsvnc->drm->mod_name) {
            free(kmsvnc->drm->mod_name);
            kmsvnc->drm->mod_name = NULL;
        }
        if (kmsvnc->drm->plane) {
            drmModeFreePlane(kmsvnc->drm->plane);
            kmsvnc->drm->plane = NULL;
        }
        if (kmsvnc->drm->mfb) {
            drmModeFreeFB2(kmsvnc->drm->mfb);
            kmsvnc->drm->mfb = NULL;
        }
        if (kmsvnc->drm->mapped) {
            munmap(kmsvnc->drm->mapped, kmsvnc->drm->mmap_size);
            kmsvnc->drm->mapped = NULL;
        }
        if (kmsvnc->drm->prime_fd > 0) {
            close(kmsvnc->drm->prime_fd);
            kmsvnc->drm->prime_fd = 0;
        }
        if (kmsvnc->drm->drm_fd > 0) {
            close(kmsvnc->drm->drm_fd);
            kmsvnc->drm->drm_fd = 0;
        }
        if (kmsvnc->drm->plane_res) {
            drmModeFreePlaneResources(kmsvnc->drm->plane_res);
            kmsvnc->drm->plane_res = NULL;
        }
        free(kmsvnc->drm);
        kmsvnc->drm = NULL;
    }
}

int drm_open() {
    struct kmsvnc_drm_data *drm = malloc(sizeof(struct kmsvnc_drm_data));
    if (!drm) KMSVNC_FATAL("memory allocation error at %s:%d\n", __FILE__, __LINE__);
    memset(drm, 0, sizeof(struct kmsvnc_drm_data));
    kmsvnc->drm = drm;

    drm->drm_fd = open(kmsvnc->card, O_RDONLY);
    if (drm->drm_fd < 0)
    {
        KMSVNC_FATAL("card %s open failed: %s\n", kmsvnc->card, strerror(errno));
    }
    drm->drm_ver = drmGetVersion(drm->drm_fd);
    printf("drm driver is %s\n", drm->drm_ver->name);

    int err = drmSetClientCap(drm->drm_fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
    if (err < 0)
    {
        perror("Failed to set universal planes capability: primary planes will not be usable");
    }
    if (kmsvnc->source_plane > 0)
    {
        drm->plane = drmModeGetPlane(drm->drm_fd, kmsvnc->source_plane);
        if (!drm->plane)
            KMSVNC_FATAL("Failed to get plane %d: %s\n", kmsvnc->source_plane, strerror(errno));
        if (drm->plane->fb_id == 0)
            fprintf(stderr, "Place %d does not have an attached framebuffer\n", kmsvnc->source_plane);
    }
    else
    {
        drm->plane_res = drmModeGetPlaneResources(drm->drm_fd);
        if (!drm->plane_res)
            KMSVNC_FATAL("Failed to get plane resources: %s\n", strerror(errno));
        int i;
        for (i = 0; i < drm->plane_res->count_planes; i++)
        {
            drm->plane = drmModeGetPlane(drm->drm_fd, drm->plane_res->planes[i]);
            if (!drm->plane)
            {
                fprintf(stderr, "Failed to get plane %u: %s\n", drm->plane_res->planes[i], strerror(errno));
                continue;
            }
            printf("Plane %u CRTC %u FB %u\n", drm->plane->plane_id, drm->plane->crtc_id, drm->plane->fb_id);
            if ((kmsvnc->source_crtc != 0 && drm->plane->crtc_id != kmsvnc->source_crtc) || drm->plane->fb_id == 0)
            {
                // Either not connected to the target source CRTC
                // or not active.
                drmModeFreePlane(drm->plane);
                drm->plane = NULL;
                continue;
            }
            break;
        }
        if (i == drm->plane_res->count_planes)
        {
            if (kmsvnc->source_crtc != 0)
            {
                KMSVNC_FATAL("No usable planes found on CRTC %d\n", kmsvnc->source_crtc);
            }
            else
            {
                KMSVNC_FATAL("No usable planes found\n");
            }
        }
        printf("Using plane %u to locate framebuffers\n", drm->plane->plane_id);
    }
    uint32_t plane_id = drm->plane->plane_id;


    drm->mfb = drmModeGetFB2(drm->drm_fd, drm->plane->fb_id);
    if (!drm->mfb) {
        KMSVNC_FATAL("Failed to get framebuffer %u: %s\n", drm->plane->fb_id, strerror(errno));
    }
    drm->pixfmt_name = drmGetFormatName(drm->mfb->pixel_format);
    drm->mod_vendor = drmGetFormatModifierVendor(drm->mfb->modifier);
    drm->mod_name = drmGetFormatModifierName(drm->mfb->modifier);
    printf("Template framebuffer is %u: %ux%u fourcc:%u mod:%lu flags:%u\n", drm->mfb->fb_id, drm->mfb->width, drm->mfb->height, drm->mfb->pixel_format, drm->mfb->modifier, drm->mfb->flags);
    printf("handles %u %u %u %u\n", drm->mfb->handles[0], drm->mfb->handles[1], drm->mfb->handles[2], drm->mfb->handles[3]);
    printf("offsets %u %u %u %u\n", drm->mfb->offsets[0], drm->mfb->offsets[1], drm->mfb->offsets[2], drm->mfb->offsets[3]);
    printf("pitches %u %u %u %u\n", drm->mfb->pitches[0], drm->mfb->pitches[1], drm->mfb->pitches[2], drm->mfb->pitches[3]);
    printf("format %s, modifier %s:%s\n", drm->pixfmt_name, drm->mod_vendor, drm->mod_name);

    if (
        drm->mfb->pixel_format != KMSVNC_FOURCC_TO_INT('X', 'R', '2', '4') &&
        drm->mfb->pixel_format != KMSVNC_FOURCC_TO_INT('A', 'R', '2', '4')
    )
    {
        KMSVNC_FATAL("Unsupported pixfmt %s, please create an issue with your pixfmt.\n", drm->pixfmt_name);
    }

    if (!drm->mfb->handles[0])
    {
        KMSVNC_FATAL("No handle set on framebuffer: maybe you need some additional capabilities?\n");
    }

    drm->mmap_fd = drm->drm_fd;
    drm->mmap_size = drm->mfb->width * drm->mfb->height * BYTES_PER_PIXEL;
    drm->funcs = malloc(sizeof(struct kmsvnc_drm_funcs));
    if (!drm->funcs) KMSVNC_FATAL("memory allocation error at %s:%d\n", __FILE__, __LINE__);
    drm->funcs->convert = convert_bgrx_to_rgb;
    drm->funcs->sync_start = drm_sync_noop;
    drm->funcs->sync_end = drm_sync_noop;

    if (drm_vendors()) return 1;

    return 0;
}


static int drm_kmsbuf_prime() {
    struct kmsvnc_drm_data *drm = kmsvnc->drm;

    int err = drmPrimeHandleToFD(drm->drm_fd, drm->mfb->handles[0], O_RDWR, &drm->prime_fd);
    if (err < 0 || drm->prime_fd < 0)
    {
        KMSVNC_FATAL("Failed to get PRIME fd from framebuffer handle");
    }
    drm->funcs->sync_start = &drm_sync_start;
    drm->funcs->sync_end = &drm_sync_end;
    drm->mmap_fd = drm->prime_fd;
    return 0;
}

static int drm_kmsbuf_prime_vaapi() {
    struct kmsvnc_drm_data *drm = kmsvnc->drm;

    int err = drmPrimeHandleToFD(drm->drm_fd, drm->mfb->handles[0], O_RDWR, &drm->prime_fd);
    if (err < 0 || drm->prime_fd < 0)
    {
        KMSVNC_FATAL("Failed to get PRIME fd from framebuffer handle");
    }

    if (va_init()) return 1;

    drm->mmap_fd = drm->prime_fd;
    drm->skip_map = 1;
    return 0;
}

static int drm_kmsbuf_dumb() {
    struct kmsvnc_drm_data *drm = kmsvnc->drm;

    struct drm_gem_flink flink;
    flink.handle = drm->mfb->handles[0];
    DRM_IOCTL_MUST(drm->drm_fd, DRM_IOCTL_GEM_FLINK, &flink);

    struct drm_gem_open open_arg;
    open_arg.name = flink.name;
    DRM_IOCTL_MUST(drm->drm_fd, DRM_IOCTL_GEM_OPEN, &open_arg);

    struct drm_mode_map_dumb mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.handle = open_arg.handle;
    DRM_IOCTL_MUST(drm->drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq);

    drm->mmap_size = open_arg.size;
    drm->mmap_offset = mreq.offset;
    return 0;
}

int drm_vendors() {
    struct kmsvnc_drm_data *drm = kmsvnc->drm;

    char *driver_name;
    if (kmsvnc->force_driver) {
        printf("using %s instead of %s\n", kmsvnc->force_driver, drm->drm_ver->name);
        driver_name = kmsvnc->force_driver;
    }
    else {
        driver_name = drm->drm_ver->name;
    }

    if (strcmp(driver_name, "i915") == 0 || strcmp(driver_name, "amdgpu") == 0)
    {
        if (fourcc_mod_is_vendor(drm->mfb->modifier, INTEL)) {
            if (strstr(drm->mod_name, "CCS")) {
                printf("warn: intel with CCS modifier detected, please set INTEL_DEBUG=noccs\n");
            }
        };
        drm->funcs->convert = &convert_vaapi;
        if (drm_kmsbuf_prime_vaapi()) return 1;
    }
    else if (strcmp(driver_name, "nvidia-drm") == 0)
    {
        printf("warn: nvidia card detected. Currently only x-tiled framebuffer is supported. Performance may suffer.\n");
        drm->funcs->convert = &convert_nvidia_x_tiled_kmsbuf;
        if (drm_kmsbuf_dumb()) return 1;
    }
    else if (strcmp(driver_name, "vmwgfx") == 0 ||
             strcmp(driver_name, "vboxvideo") == 0 ||
             strcmp(driver_name, "virtio_gpu") == 0
    )
    {
        if (drm->mfb->modifier != DRM_FORMAT_MOD_NONE && drm->mfb->modifier != DRM_FORMAT_MOD_LINEAR) {
            printf("warn: modifier is not LINEAR, please create an issue with your modifier.\n");
        }
        // virgl does not work
        if (drm_kmsbuf_dumb()) return 1;
    }
    else if (strcmp(driver_name, "test-prime") == 0)
    {
        if (drm_kmsbuf_prime()) return 1;
    }
    else if (strcmp(driver_name, "test-map-dumb") == 0)
    {
        if (drm_kmsbuf_dumb()) return 1;
    }
    else if (strcmp(driver_name, "test-i915-gem") == 0)
    {
        struct drm_gem_flink flink;
        flink.handle = drm->mfb->handles[0];
        DRM_IOCTL_MUST(drm->drm_fd, DRM_IOCTL_GEM_FLINK, &flink);

        struct drm_gem_open open_arg;
        open_arg.name = flink.name;
        DRM_IOCTL_MUST(drm->drm_fd, DRM_IOCTL_GEM_OPEN, &open_arg);

        struct drm_i915_gem_mmap_gtt mmap_arg;
        mmap_arg.handle = open_arg.handle;
        DRM_IOCTL_MUST(drm->drm_fd, DRM_IOCTL_I915_GEM_MMAP_GTT, &mmap_arg);
        drm->mmap_size = open_arg.size;
        drm->mmap_offset = mmap_arg.offset;
    }
    else if (strcmp(driver_name, "test-i915-prime-xtiled") == 0)
    {
        drm->funcs->convert = &convert_intel_x_tiled_kmsbuf;
        if (drm_kmsbuf_prime()) return 1;
    }
    else
    {
        fprintf(stderr, "Untested drm driver, use at your own risk!\n");
        if (drm->mfb->modifier != DRM_FORMAT_MOD_NONE && drm->mfb->modifier != DRM_FORMAT_MOD_LINEAR) {
            printf("warn: modifier is not LINEAR, please create an issue with your driver and modifier.\n");
        }
        if (drm_kmsbuf_dumb()) return 1;
    }

    if (!drm->skip_map && !drm->mapped)
    {
        printf("mapping with size = %lu, offset = %ld, fd = %d\n", drm->mmap_size, drm->mmap_offset, drm->mmap_fd);
        drm->mapped = mmap(NULL, drm->mmap_size, PROT_READ, MAP_SHARED, drm->mmap_fd, drm->mmap_offset);
        if (drm->mapped == MAP_FAILED)
        {
            KMSVNC_FATAL("Failed to mmap: %s\n", strerror(errno));
        }
    }

    return 0;
}
