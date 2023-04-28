#include <va/va_drm.h>
#include <va/va_drmcommon.h>
#include <fcntl.h>

#include "va.h"
#include "kmsvnc.h"

extern struct kmsvnc_data *kmsvnc;

void va_cleanup() {
    VAStatus s;
    if (kmsvnc->va) {
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
    #ifdef KMSVNC_VA_DEBUG
    printf("va msg: %s");
    #endif
}

static void va_error_callback(void *user_context, const char *message) {
    printf("va error: %s");
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

    prime_desc.fourcc = kmsvnc->drm->mfb->pixel_format == KMSVNC_FOURCC_TO_INT('X', 'R', '2', '4') ?
        KMSVNC_FOURCC_TO_INT('B', 'G', 'R', 'X') :
        KMSVNC_FOURCC_TO_INT('B', 'G', 'R', 'A') ;
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
    if ((s = vaCreateSurfaces(va->dpy, VA_RT_FORMAT_RGB32,
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


        VA_MUST(vaCreateSurfaces(va->dpy, VA_RT_FORMAT_RGB32,
                kmsvnc->drm->mfb->width, kmsvnc->drm->mfb->height, &va->surface_id, 1,
                buffer_attrs, KMSVNC_ARRAY_ELEMENTS(buffer_attrs)));
    }

    #ifdef KMSVNC_VA_DEBUG
    int img_fmt_count = vaMaxNumImageFormats(va->dpy);
    VAImageFormat *img_fmts = malloc(sizeof(VAImageFormat) * img_fmt_count);
    if (!img_fmts) KMSVNC_FATAL("memory allocation error at %s:%d\n", __FILE__, __LINE__);
    {
        int got;
        vaQueryImageFormats(va->dpy, img_fmts, &got);
        if (got != img_fmt_count) {
            KMSVNC_FATAL("got less VAImageFormats, %d instead of %d\n", got, img_fmt_count);
        }
    }

    for (int i = 0; i < img_fmt_count; i++) {
        printf("fmt %d: fourcc %d, %c%c%c%c, byte_order %s, bpp %d, depth %d, blue_mask %#x, green_mask %#x, red_mask %#x, reserved %#x\n", i, img_fmts[i].fourcc,
            img_fmts[i].fourcc & 0xff,
            img_fmts[i].fourcc >> 8 & 0xff,
            img_fmts[i].fourcc >> 16 & 0xff,
            img_fmts[i].fourcc >> 24 & 0xff,
            img_fmts[i].byte_order - 1 ? "VA_LSB_FIRST" : "VA_MSB_FIRST",
            img_fmts[i].bits_per_pixel,
            img_fmts[i].depth,
            img_fmts[i].blue_mask,
            img_fmts[i].green_mask,
            img_fmts[i].red_mask,
            img_fmts[i].va_reserved
        );
    }
    #endif

    VAImageFormat format = {
        .fourcc = KMSVNC_FOURCC_TO_INT('R','G','B','X'),
        .byte_order = VA_LSB_FIRST,
        .bits_per_pixel = 32,
        .depth = 24,
        .blue_mask = 0x000000ff,
        .green_mask = 0x0000ff00,
        .red_mask = 0x00ff0000,
        .va_reserved = 0x00000000,
    };
    va->image = malloc(sizeof(VAImage));
    if (!va->image) KMSVNC_FATAL("memory allocation error at %s:%d\n", __FILE__, __LINE__);
    if ((s = vaCreateImage(va->dpy, &format, kmsvnc->drm->mfb->width, kmsvnc->drm->mfb->height, va->image)) != VA_STATUS_SUCCESS) {
        free(va->image);
        va->image = NULL;
        VA_MUST(s);
    }
    VA_MUST(vaMapBuffer(va->dpy, va->image->buf, (void**)&va->imgbuf));
}

int va_hwframe_to_vaapi(char *out) {
    VA_MUST(vaGetImage(kmsvnc->va->dpy, kmsvnc->va->surface_id, 0, 0,
                        kmsvnc->drm->mfb->width, kmsvnc->drm->mfb->height, kmsvnc->va->image->image_id));
    memcpy(out, kmsvnc->va->imgbuf, kmsvnc->drm->mfb->width * kmsvnc->drm->mfb->height * BYTES_PER_PIXEL);
}
