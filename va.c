#include <va/va_drm.h>
#include <va/va_drmcommon.h>
#include <fcntl.h>

#include "va.h"
#include "kmsvnc.h"

extern struct kmsvnc_data *kmsvnc;

void va_cleanup() {
    VAStatus s;
    if (kmsvnc->va) {
        if (kmsvnc->va->img_fmts) {
            free(kmsvnc->va->img_fmts);
            kmsvnc->va->img_fmts = NULL;
        }
        if (kmsvnc->va->imgbuf) {
            VA_MAY(vaUnmapBuffer(kmsvnc->va->dpy, kmsvnc->va->image->buf));
            kmsvnc->va->imgbuf = NULL;
        }
        if (kmsvnc->va->image) {
            if ((s = vaDestroyImage(kmsvnc->va->dpy, kmsvnc->va->image->image_id)) == VA_STATUS_SUCCESS) {
                free(kmsvnc->va->image);
            }
            VA_MAY(s);
            kmsvnc->va->image = NULL;
        }
        if (kmsvnc->va->surface_id > 0) {
            VA_MAY(vaDestroySurfaces(kmsvnc->va->dpy, &kmsvnc->va->surface_id, 1));
            kmsvnc->va->surface_id = 0;
        }
        if (kmsvnc->va->dpy) {
            VA_MAY(vaTerminate(kmsvnc->va->dpy));
            kmsvnc->va->dpy = NULL;
        }
        free(kmsvnc->va);
        kmsvnc->va = NULL;
    }
}

static void va_msg_callback(void *user_context, const char *message) {
    if (kmsvnc->va_print_fmt) {
        printf("va msg: %s", message);
    }
}

static void va_error_callback(void *user_context, const char *message) {
    printf("va error: %s", message);
}

static char* fourcc_to_str(int fourcc) {
    static char ret[5];
    ret[4] = 0;
    for (int i = 0; i < 4; i++) {
        ret[i] = fourcc >> 8*i & 0xff;
    }
    return ret;
}

static const struct {
    uint32_t drm_fourcc;
    uint32_t va_fourcc;
    uint32_t va_rt_format;
    char alpha;
} va_format_map[] = {
    {KMSVNC_FOURCC_TO_INT('X', 'R', '2', '4'), KMSVNC_FOURCC_TO_INT('B', 'G', 'R', 'X'), VA_RT_FORMAT_RGB32, 0},
    {KMSVNC_FOURCC_TO_INT('A', 'R', '2', '4'), KMSVNC_FOURCC_TO_INT('B', 'G', 'R', 'A'), VA_RT_FORMAT_RGB32, 1},
    {KMSVNC_FOURCC_TO_INT('X', 'R', '3', '0'), KMSVNC_FOURCC_TO_INT('X', 'R', '3', '0'), VA_RT_FORMAT_RGB32_10, 0},
    {KMSVNC_FOURCC_TO_INT('A', 'R', '3', '0'), KMSVNC_FOURCC_TO_INT('A', 'R', '3', '0'), VA_RT_FORMAT_RGB32_10, 1},
};

static void print_va_image_fmt(VAImageFormat *fmt) {
        printf("image fmt: fourcc %d, %s, byte_order %s, bpp %d, depth %d, blue_mask %#x, green_mask %#x, red_mask %#x, reserved %#x %#x %#x %#x\n", fmt->fourcc,
            fourcc_to_str(fmt->fourcc),
            fmt->byte_order == 1 ? "VA_LSB_FIRST" : "VA_MSB_FIRST",
            fmt->bits_per_pixel,
            fmt->depth,
            fmt->blue_mask,
            fmt->green_mask,
            fmt->red_mask,
            fmt->va_reserved[0],
            fmt->va_reserved[1],
            fmt->va_reserved[2],
            fmt->va_reserved[3]
        );
}

int va_init() {
    if (!kmsvnc->drm || !kmsvnc->drm->drm_fd || !kmsvnc->drm->prime_fd) {
        KMSVNC_FATAL("drm is not initialized\n");
    }

    setenv("DISPLAY", "", 1);

    struct kmsvnc_va_data *va = malloc(sizeof(struct kmsvnc_va_data));
    if (!va) KMSVNC_FATAL("memory allocation error at %s:%d\n", __FILE__, __LINE__);
    memset(va, 0, sizeof(struct kmsvnc_va_data));
    kmsvnc->va = va;

    char* render_node;
    int effective_fd = 0;
    if (render_node = drmGetRenderDeviceNameFromFd(kmsvnc->drm->drm_fd)) {
        va->render_node_fd = open(render_node, O_RDWR);
        free(render_node);
    }
    else {
        printf("Using non-render node because the device does not have an associated render node.\n");
    }
    if (va->render_node_fd > 0) {
        effective_fd = va->render_node_fd;
    }
    else {
        printf("Using non-render node because render node fails to open.\n");
        effective_fd = kmsvnc->drm->drm_fd;
    }

    va->dpy = vaGetDisplayDRM(effective_fd);
    if (!va->dpy) {
        KMSVNC_FATAL("vaGetDisplayDRM failed\n");
    }

    vaSetErrorCallback(va->dpy, &va_error_callback, NULL);
    vaSetInfoCallback(va->dpy, &va_msg_callback, NULL);

    int major, minor;
    VAStatus status;
    VA_MUST(vaInitialize(va->dpy, &major, &minor));

    const char *vendor_string = vaQueryVendorString(va->dpy);
    printf("vaapi vendor %s\n", vendor_string);

    VADRMPRIMESurfaceDescriptor prime_desc;
    VASurfaceAttrib prime_attrs[2] = {
        {
            .type  = VASurfaceAttribMemoryType,
            .flags = VA_SURFACE_ATTRIB_SETTABLE,
            .value.type    = VAGenericValueTypeInteger,
            .value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
        },
        {
            .type  = VASurfaceAttribExternalBufferDescriptor,
            .flags = VA_SURFACE_ATTRIB_SETTABLE,
            .value.type    = VAGenericValueTypePointer,
            .value.value.p = &prime_desc,
        }
    };

    uint32_t rt_format = 0;
    char is_alpha = 0;
    for (int i = 0; i < KMSVNC_ARRAY_ELEMENTS(va_format_map); i++) {
        if (kmsvnc->drm->mfb->pixel_format == va_format_map[i].drm_fourcc) {
            prime_desc.fourcc = va_format_map[i].va_fourcc;
            rt_format = va_format_map[i].va_rt_format;
            is_alpha = va_format_map[i].alpha;
            break;
        }
    }
    if (!rt_format) {
        KMSVNC_FATAL("Unsupported pixfmt %s for vaapi, please create an issue with your pixfmt.", kmsvnc->drm->pixfmt_name);
    }
    if (kmsvnc->va_print_fmt) {
        printf("selected rt_format %u, alpha %d\n", rt_format, is_alpha);
    }
    prime_desc.width = kmsvnc->drm->mfb->width;
    prime_desc.height = kmsvnc->drm->mfb->height;

    int i;
    int max_size = 0;
    for (i = 0; i < 4; i++) {
        int size = kmsvnc->drm->mfb->offsets[i] + kmsvnc->drm->mfb->height * kmsvnc->drm->mfb->pitches[i];
        if (size > max_size) max_size = size;
    }
    for (i = 0; i < 4; i++) {
        prime_desc.objects[i].fd = kmsvnc->drm->prime_fd;
        prime_desc.objects[i].size = max_size;
        prime_desc.objects[i].drm_format_modifier = kmsvnc->drm->mfb->modifier;
    }

    prime_desc.num_layers = 1;
    prime_desc.layers[0].drm_format = kmsvnc->drm->mfb->pixel_format;
    for (i = 0; i < 4; i++) {
        prime_desc.layers[0].object_index[i] = 0;
        prime_desc.layers[0].offset[i] = kmsvnc->drm->mfb->offsets[i];
        prime_desc.layers[0].pitch[i] = kmsvnc->drm->mfb->pitches[i];
    }
    for (i = 0; i < 4; i++) {
        if (!kmsvnc->drm->mfb->handles[i]) {
            break;
        }
    }
    prime_desc.layers[0].num_planes = i;
    prime_desc.num_objects = 1;

    VAStatus s;
    if ((s = vaCreateSurfaces(va->dpy, rt_format,
                            kmsvnc->drm->mfb->width, kmsvnc->drm->mfb->height, &va->surface_id, 1,
                            prime_attrs, KMSVNC_ARRAY_ELEMENTS(prime_attrs))) != VA_STATUS_SUCCESS)
    {
        printf("vaCreateSurfaces prime2 error %#x %s, trying prime\n", s, vaErrorStr(s));

        VASurfaceAttribExternalBuffers buffer_desc;
        VASurfaceAttrib buffer_attrs[2] = {
            {
                .type  = VASurfaceAttribMemoryType,
                .flags = VA_SURFACE_ATTRIB_SETTABLE,
                .value.type    = VAGenericValueTypeInteger,
                .value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME,
            },
            {
                .type  = VASurfaceAttribExternalBufferDescriptor,
                .flags = VA_SURFACE_ATTRIB_SETTABLE,
                .value.type    = VAGenericValueTypePointer,
                .value.value.p = &buffer_desc,
            }
        };

        unsigned long fd = kmsvnc->drm->prime_fd;

        buffer_desc.pixel_format = prime_desc.fourcc;
        buffer_desc.width        = kmsvnc->drm->mfb->width;
        buffer_desc.height       = kmsvnc->drm->mfb->height;
        buffer_desc.data_size    = max_size;
        buffer_desc.buffers      = &fd;
        buffer_desc.num_buffers  = 1;
        buffer_desc.flags        = 0;

        for (i = 0; i < 4; i++) {
            buffer_desc.pitches[i] = kmsvnc->drm->mfb->pitches[i];
            buffer_desc.offsets[i] = kmsvnc->drm->mfb->offsets[i];
        }
        buffer_desc.num_planes = prime_desc.layers[0].num_planes;


        VA_MUST(vaCreateSurfaces(va->dpy, rt_format,
                kmsvnc->drm->mfb->width, kmsvnc->drm->mfb->height, &va->surface_id, 1,
                buffer_attrs, KMSVNC_ARRAY_ELEMENTS(buffer_attrs)));
    }


    va->img_fmt_count = vaMaxNumImageFormats(va->dpy);
    va->img_fmts = malloc(sizeof(VAImageFormat) * va->img_fmt_count);
    if (!va->img_fmts) KMSVNC_FATAL("memory allocation error at %s:%d\n", __FILE__, __LINE__);
    {
        int got;
        vaQueryImageFormats(va->dpy, va->img_fmts, &got);
        if (got != va->img_fmt_count) {
            printf("got less VAImageFormats, %d instead of %d\n", got, va->img_fmt_count);
            va->img_fmt_count = got;
        }
    }

    if (kmsvnc->va_print_fmt) {
        for (int i = 0; i < va->img_fmt_count; i++) {
            print_va_image_fmt(va->img_fmts + i);
        }
    }

    struct fourcc_data {
        uint32_t va_fourcc;
        VAImageFormat *fmt;
        char is_alpha;
        uint32_t va_rt_format;
    };
    struct fourcc_data format_to_try[] = {
        {KMSVNC_FOURCC_TO_INT('R','G','B','X'), NULL, 0, VA_RT_FORMAT_RGB32},
        {KMSVNC_FOURCC_TO_INT('R','G','B','A'), NULL, 1, VA_RT_FORMAT_RGB32},
        {KMSVNC_FOURCC_TO_INT('X','R','G','B'), NULL, 0, VA_RT_FORMAT_RGB32},
        {KMSVNC_FOURCC_TO_INT('A','R','G','B'), NULL, 1, VA_RT_FORMAT_RGB32},

        {KMSVNC_FOURCC_TO_INT('B','G','R','X'), NULL, 0, VA_RT_FORMAT_RGB32},
        {KMSVNC_FOURCC_TO_INT('B','G','R','A'), NULL, 1, VA_RT_FORMAT_RGB32},
        {KMSVNC_FOURCC_TO_INT('X','B','G','R'), NULL, 0, VA_RT_FORMAT_RGB32},
        {KMSVNC_FOURCC_TO_INT('A','B','G','R'), NULL, 1, VA_RT_FORMAT_RGB32},

        {KMSVNC_FOURCC_TO_INT('X','R','3','0'), NULL, 0, VA_RT_FORMAT_RGB32_10},
        {KMSVNC_FOURCC_TO_INT('A','R','3','0'), NULL, 1, VA_RT_FORMAT_RGB32_10},
        {KMSVNC_FOURCC_TO_INT('X','B','3','0'), NULL, 0, VA_RT_FORMAT_RGB32_10},
        {KMSVNC_FOURCC_TO_INT('A','B','3','0'), NULL, 1, VA_RT_FORMAT_RGB32_10},
    };
    for (int i = 0; i < va->img_fmt_count; i++) {
        for (int j = 0; j < KMSVNC_ARRAY_ELEMENTS(format_to_try); j++) {
            if (va->img_fmts[i].fourcc == format_to_try[j].va_fourcc) {
                format_to_try[j].fmt = va->img_fmts + i;
            }
        }
    }

    va->image = malloc(sizeof(VAImage));
    if (!va->image) KMSVNC_FATAL("memory allocation error at %s:%d\n", __FILE__, __LINE__);

    va->derive_enabled = 0;
    va->derive_enabled = kmsvnc->va_derive_enabled < 0 ? va->derive_enabled : kmsvnc->va_derive_enabled != 0;
    if (va->derive_enabled) {
        if ((s = vaDeriveImage(va->dpy, va->surface_id, va->image)) == VA_STATUS_SUCCESS) {
            char found = 0;
            for (int i = 0; i < KMSVNC_ARRAY_ELEMENTS(format_to_try); i++) {
                if (va->image->format.fourcc == format_to_try[i].fmt->fourcc) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                va->derive_enabled = 0;
                printf("vaDeriveImage returned unknown fourcc %d %s\n", va->image->format.fourcc, fourcc_to_str(va->image->format.fourcc));
                VA_MAY(vaDestroyImage(kmsvnc->va->dpy, kmsvnc->va->image->image_id));
            }
        }
        VA_MAY(s);
    }
    if (va->derive_enabled) {
        if ((s = vaMapBuffer(va->dpy, va->image->buf, (void**)&va->imgbuf)) != VA_STATUS_SUCCESS) {
            VA_MAY(s);
            VA_MAY(vaDestroyImage(kmsvnc->va->dpy, kmsvnc->va->image->image_id));
            va->derive_enabled = 0;
        }
    }
    if (!va->derive_enabled) {
        char success = 0;
        for (int i = 0; i < KMSVNC_ARRAY_ELEMENTS(format_to_try); i++) {
            if (format_to_try[i].fmt == NULL) continue;
            if (!kmsvnc->va_print_fmt && rt_format != format_to_try[i].va_rt_format) continue;
            if (is_alpha != format_to_try[i].is_alpha) continue;

            VAImageFormat *fmt = format_to_try[i].fmt;
            if ((s = vaCreateImage(va->dpy, fmt, kmsvnc->drm->mfb->width, kmsvnc->drm->mfb->height, va->image)) != VA_STATUS_SUCCESS) {
                VA_MAY(s);
                continue;
            }
            if ((s = vaMapBuffer(va->dpy, va->image->buf, (void**)&va->imgbuf)) != VA_STATUS_SUCCESS) {
                VA_MAY(s);
                VA_MAY(vaDestroyImage(kmsvnc->va->dpy, kmsvnc->va->image->image_id));
                continue;
            }
            if ((s = vaGetImage(kmsvnc->va->dpy, kmsvnc->va->surface_id, 0, 0,
                    kmsvnc->drm->mfb->width, kmsvnc->drm->mfb->height,
                    kmsvnc->va->image->image_id)) != VA_STATUS_SUCCESS)
            {
                VA_MAY(s);
                VA_MAY(vaUnmapBuffer(kmsvnc->va->dpy, kmsvnc->va->image->buf));
                VA_MAY(vaDestroyImage(kmsvnc->va->dpy, kmsvnc->va->image->image_id));
                continue;
            }
            else {
                success = 1;
                break;
            }
        }
        if (!success) {
            va->imgbuf = NULL;
            KMSVNC_FATAL("failed to get vaapi image\n");
        }
    }
    printf("got vaapi %simage:\n", va->derive_enabled ? "derive " : "");
    print_va_image_fmt(&va->image->format);
    return 0;
}

int va_hwframe_to_vaapi(char *out) {
    if (!kmsvnc->va->derive_enabled) {
        VA_MUST(vaGetImage(kmsvnc->va->dpy, kmsvnc->va->surface_id, 0, 0,
                kmsvnc->drm->mfb->width, kmsvnc->drm->mfb->height, kmsvnc->va->image->image_id));
    }
    memcpy(out, kmsvnc->va->imgbuf, kmsvnc->drm->mfb->width * kmsvnc->drm->mfb->height * BYTES_PER_PIXEL);
    return 0;
}
