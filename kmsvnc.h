#pragma once

#include <rfb/rfb.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

#include <xf86drm.h>
#include <i915_drm.h>
#include <amdgpu_drm.h>
#include <xf86drmMode.h>
#include <linux/dma-buf.h>
#include <va/va.h>


#define BYTES_PER_PIXEL 4
#define CURSOR_FRAMESKIP 15

struct vnc_opt
{
    int port;
    struct in_addr *bind;
    char *bind6;
    char disable_ipv6;
    int sleep_ns;
    char always_shared;
    char disable_cmpfb;
    char *desktop_name;
};

struct kmsvnc_data
{
    char *debug_capture_fb;
    char *card;
    char *force_driver;
    struct vnc_opt *vnc_opt;
    char input_wakeup;
    char disable_input;
    int va_derive_enabled;
    char debug_enabled;
    int source_plane;
    int source_crtc;
    int input_width;
    int input_height;
    int input_offx;
    int input_offy;
    char screen_blank;
    char screen_blank_restore;
    struct kmsvnc_drm_data *drm;
    struct kmsvnc_input_data *input;
    struct kmsvnc_keymap_data *keymap;
    struct kmsvnc_va_data *va;
    rfbScreenInfoPtr server;
    char shutdown;
    char capture_cursor;
    char *cursor_bitmap;
    int cursor_bitmap_len;
    char *buf;
    char *buf1;
};



struct key_iter_search
{
    xkb_keysym_t keysym;

    xkb_keycode_t keycode;
    xkb_level_index_t level;
};

struct kmsvnc_keymap_data
{
    struct xkb_context *ctx;
    struct xkb_keymap *map;
};


struct kmsvnc_input_data {
    int uinput_fd;
    char *keystate;
};


struct kmsvnc_drm_funcs
{
    void (*sync_start)(int);
    void (*sync_end)(int);
    void (*convert)(const char *, int, int, char *);
};

struct kmsvnc_drm_gamma_data
{
    uint32_t size;
    uint16_t *red;
    uint16_t *green;
    uint16_t *blue;
};

struct kmsvnc_drm_data
{
    int drm_fd;
    int drm_master_fd;
    drmVersionPtr drm_ver;
    int prime_fd;
    drmModePlane *plane;
    drmModePlane *cursor_plane;
    drmModePlaneRes *plane_res;
    drmModeFB2 *mfb;
    drmModeFB2 *cursor_mfb;
    uint32_t plane_id;
    int mmap_fd;
    size_t mmap_size;
    off_t mmap_offset;
    char *mapped;
    char *cursor_mapped;
    size_t cursor_mmap_size;
    char skip_map;
    struct kmsvnc_drm_funcs *funcs;
    char *pixfmt_name;
    char *mod_vendor;
    char *mod_name;
    char *kms_convert_buf;
    size_t kms_convert_buf_len;
    char *kms_cpy_tmp_buf;
    size_t kms_cpy_tmp_buf_len;
    char *kms_cursor_buf;
    size_t kms_cursor_buf_len;
    struct kmsvnc_drm_gamma_data *gamma;
};

struct kmsvnc_va_data
{
    VADisplay dpy;
    int render_node_fd;
    VASurfaceID surface_id;
    VAImage *image;
    char *imgbuf;
    char derive_enabled;
    VAImageFormat* img_fmts;
    int img_fmt_count;
    VAImageFormat* selected_fmt;
};

#define KMSVNC_FATAL(...) do{ fprintf(stderr, __VA_ARGS__); return 1; } while(0)
#define KMSVNC_ARRAY_ELEMENTS(x) (sizeof(x) / sizeof(x[0]))
#define KMSVNC_FOURCC_TO_INT(a,b,c,d) (((a) << 0) + ((b) << 8) + ((c) << 16) + ((d) << 24))
#define KMSVNC_WRITE_MAY(fd,buf,count) do { ssize_t e = write((fd), (buf), (count)); if (e != (count)) fprintf(stderr, "should write %ld bytes, actually wrote %ld, on line %d\n", (count), e, __LINE__); } while (0)

#define KMSVNC_DEBUG(...) do{ if (kmsvnc->debug_enabled) fprintf(stdout, __VA_ARGS__); } while(0)
