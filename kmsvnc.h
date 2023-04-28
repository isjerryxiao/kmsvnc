#pragma once

#include <rfb/rfb.h>
#include <xkbcommon/xkbcommon.h>

#include <xf86drm.h>
#include <i915_drm.h>
#include <amdgpu_drm.h>
#include <xf86drmMode.h>
#include <linux/dma-buf.h>


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
    char disable_input;
    int source_plane;
    int source_crtc;
    struct kmsvnc_drm_data *drm;
    struct kmsvnc_input_data *input;
    struct kmsvnc_keymap_data *keymap;
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
    struct kmsvnc_drm_funcs *funcs;
};

