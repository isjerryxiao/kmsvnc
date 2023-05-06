#pragma once

#include <rfb/rfb.h>
#include <xkbcommon/xkbcommon.h>

#include <xf86drm.h>
#include <i915_drm.h>
#include <amdgpu_drm.h>
#include <xf86drmMode.h>
#include <linux/dma-buf.h>
#include <va/va.h>


#define BYTES_PER_PIXEL 4

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
    int source_plane;
    int source_crtc;
    struct kmsvnc_drm_data *drm;
    struct kmsvnc_input_data *input;
    struct kmsvnc_keymap_data *keymap;
    struct kmsvnc_va_data *va;
    rfbScreenInfoPtr server;
    char shutdown;
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

struct kmsvnc_drm_data
{
    int drm_fd;
    drmVersionPtr drm_ver;
    int prime_fd;
    drmModePlane *plane;
    drmModePlaneRes *plane_res;
    drmModeFB2 *mfb;
    u_int32_t plane_id;
    int mmap_fd;
    size_t mmap_size;
    off_t mmap_offset;
    char *mapped;
    char skip_map;
    struct kmsvnc_drm_funcs *funcs;
    char *pixfmt_name;
    char *mod_vendor;
    char *mod_name;
};

struct kmsvnc_va_data
{
    VADisplay dpy;
    int render_node_fd;
    VASurfaceID surface_id;
    VAImage *image;
    char *imgbuf;
    char is_bgr;  // bgr -> rgb
    char is_xrgb; // shift 8
    char derive_enabled;
};

#define KMSVNC_FATAL(...) do{ fprintf(stderr, __VA_ARGS__); return 1; } while(0)
#define KMSVNC_ARRAY_ELEMENTS(x) (sizeof(x) / sizeof(x[0]))
#define KMSVNC_FOURCC_TO_INT(a,b,c,d) (((a) << 0) + ((b) << 8) + ((c) << 16) + ((d) << 24))
#define KMSVNC_WRITE_MAY(fd,buf,count) do { ssize_t e = write((fd), (buf), (count)); if (e != (count)) fprintf(stderr, "should write %ld bytes, actually wrote %ld, on line %d\n", (count), e, __LINE__); } while (0)
