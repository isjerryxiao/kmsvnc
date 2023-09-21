#define _GNU_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <argp.h>
#include <arpa/inet.h>

#include "kmsvnc.h"
#include "keymap.h"
#include "input.h"
#include "drm.h"
#include "va.h"

struct kmsvnc_data *kmsvnc = NULL;

#define NS_IN_S 1000000000

static void between_frames()
{
    static struct timespec now = {0, 0}, then = {0, 0}, tmp = {0, 0};

    clock_gettime(CLOCK_MONOTONIC, &now);
    memcpy((char *)&then, (char *)&tmp, sizeof(struct timespec));
    tmp.tv_nsec += kmsvnc->vnc_opt->sleep_ns;
    if (tmp.tv_nsec >= NS_IN_S)
    {
        tmp.tv_sec++;
        tmp.tv_nsec %= NS_IN_S;
    }
    if (now.tv_sec < tmp.tv_sec || (now.tv_sec == tmp.tv_sec && now.tv_nsec < tmp.tv_nsec))
    {
        then.tv_sec = tmp.tv_sec - now.tv_sec;
        then.tv_nsec = tmp.tv_nsec - now.tv_nsec;
        if (then.tv_nsec < 0)
        {
            then.tv_sec--;
            then.tv_nsec += NS_IN_S;
        }
        nanosleep(&then, &then);
    }
    memcpy((char *)&now, (char *)&then, sizeof(struct timespec));
}

static void update_screen_buf(char* to, char *from, int width, int height) {
    uint64_t *double_pix_from = (uint64_t *)from;
    uint64_t *double_pix_to = (uint64_t *)to;
    int min_x = INT32_MAX;
    int min_y = INT32_MAX;
    int max_x = -1;
    int max_y = -1;
    if (!kmsvnc->vnc_opt->disable_cmpfb && width % 2 == 0) {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x+=2) {
                if (*double_pix_from != *double_pix_to) {
                    if (x < min_x) {
                        min_x = x;
                    }
                    if (x > max_x) {
                        max_x = x;
                    }
                    if (y < min_y) {
                        min_y = y;
                    }
                    if (y > max_y) {
                        max_y = y;
                    }
                }
                double_pix_from ++;
                double_pix_to ++;
            }
        }
    }
    else {
        memcpy(to, from, width * height * BYTES_PER_PIXEL);
        rfbMarkRectAsModified(kmsvnc->server, 0, 0, width, height);
        return;
    }
    max_x = max_x < 0 ? 0 : max_x;
    max_y = max_y < 0 ? 0 : max_y;
    min_x = min_x > width ? 0 : min_x;
    min_y = min_y > height ? 0 : min_y;

    //printf("dirty: %d, %d, %d, %d\n", min_x, min_y, max_x, max_y);
    if (max_x || max_y || min_x || min_y) {
        memcpy(to, from, width * height * BYTES_PER_PIXEL);
        rfbMarkRectAsModified(kmsvnc->server, min_x, min_y, max_x + 2, max_y + 1);
    }
}

static inline void update_vnc_cursor(char *data, int width, int height) {
    uint8_t r, g, b, a;
    #define CURSOR_MIN_A 160 // ~63%
    int min_x = width;
    int max_x = -1;
    int min_y = height;
    int max_y = -1;
    int x, y;

    for (int i = 0; i < width * height * BYTES_PER_PIXEL; i += BYTES_PER_PIXEL) {
        uint32_t pixdata = htonl(*((uint32_t*)(data + i)));
        //r = (pixdata & 0xff000000u) >> 24;
        //g = (pixdata & 0x00ff0000u) >> 16;
        //b = (pixdata & 0x0000ff00u) >> 8;
        a = pixdata & 0xff;
        if (a > CURSOR_MIN_A) {
            x = (i / BYTES_PER_PIXEL) % width;
            y = (i / BYTES_PER_PIXEL) / width;
            if (x < min_x) min_x = x;
            if (y < min_y) min_y = y;
            if (x > max_x) max_x = x;
            if (y > max_y) max_y = y;
        }
    }
    if (min_x > max_x || min_y > max_y) {
        // no cursor detected
        return;
    }
    int rwidth = max_x - min_x + 1;
    int rheight = max_y - min_y + 1;
    if (kmsvnc->cursor_bitmap_len < rwidth * rheight * BYTES_PER_PIXEL)
    {
        if (kmsvnc->cursor_bitmap)
            free(kmsvnc->cursor_bitmap);
        kmsvnc->cursor_bitmap = malloc(rwidth * rheight * BYTES_PER_PIXEL);
        if (!kmsvnc->cursor_bitmap) return;
        kmsvnc->cursor_bitmap_len = rwidth * rheight * BYTES_PER_PIXEL;
    }
    unsigned char *rich_source = malloc(rwidth * rheight * BYTES_PER_PIXEL);
    if (!rich_source) return;
    char *maskString = malloc(rwidth * rheight);
    if (!maskString) {
        free(rich_source);
        return;
    }
    memset(maskString, ' ', rwidth * rheight);
    for (int i = 0; i < rwidth; i++) {
        for (int j = 0; j < rheight; j++) {
            int t = (i + j * rwidth) * BYTES_PER_PIXEL;
            int s = ((i+min_x) + (j+min_y) * width) * BYTES_PER_PIXEL;
            *((uint32_t*)(rich_source + t)) = *((uint32_t*)(data + s));
            if ((uint8_t)*(rich_source + t + 3) > CURSOR_MIN_A) {
                maskString[i + j * rwidth] = 'x';
            }
        }
    }

    if ((kmsvnc->server->cursor->width != rwidth || kmsvnc->server->cursor->height != rheight) || memcmp(kmsvnc->cursor_bitmap, rich_source, rwidth * rheight * BYTES_PER_PIXEL)) {
        KMSVNC_DEBUG("cursor update %dx%d\n", rwidth, rheight);
        memcpy(kmsvnc->cursor_bitmap, rich_source, kmsvnc->cursor_bitmap_len);
        char *cursorString = malloc(rwidth * rheight);
        if (!cursorString) {
            free(rich_source);
            free(maskString);
            return;
        }

        memset(cursorString, 'x', rwidth * rheight);

        rfbCursorPtr cursor = rfbMakeXCursor(rwidth, rheight, cursorString, maskString);
        free(cursorString);
        cursor->richSource = rich_source;
        cursor->cleanupRichSource = TRUE;
        cursor->xhot = 0;
        cursor->yhot = 0;
        rfbSetCursor(kmsvnc->server, cursor);
    }
    else {
        free(rich_source);
        free(maskString);
    }
}

static void cleanup() {
    if (kmsvnc->keymap) {
        xkb_cleanup();
    }
    if (kmsvnc->input) {
        uinput_cleanup();
    }
    if (kmsvnc->drm) {
        drm_cleanup();
    }
    if (kmsvnc->va) {
        va_cleanup();
    }
    if (kmsvnc) {
        if (kmsvnc->vnc_opt) {
            free(kmsvnc->vnc_opt);
            kmsvnc->vnc_opt = NULL;
        }
        if (kmsvnc->buf1) {
            free(kmsvnc->buf1);
            kmsvnc->buf1 = NULL;
        }
        if (kmsvnc->buf) {
            free(kmsvnc->buf);
            kmsvnc->buf = NULL;
        }
        if (kmsvnc->cursor_bitmap) {
            free(kmsvnc->cursor_bitmap);
            kmsvnc->cursor_bitmap = NULL;
        }
        kmsvnc->cursor_bitmap_len = 0;
        free(kmsvnc);
        kmsvnc = NULL;
    }
}

void signal_handler_noop(int signum){}
void signal_handler(int signum){
    if (kmsvnc->shutdown) {
        return;
    }
    kmsvnc->shutdown = 1;
    if (kmsvnc->server) {
        rfbShutdownServer(kmsvnc->server,TRUE);
    }
}

static struct argp_option kmsvnc_main_options[] = {
    {"device", 'd', "/dev/dri/card0", 0, "DRM device"},
    {"source-plane", 0xfefc, "0", 0, "Use specific plane"},
    {"source-crtc", 0xfefd, "0", 0, "Use specific crtc (to list all crtcs and planes, set this to -1)"},
    {"force-driver", 0xfefe, "i915", 0, "force a certain driver (for debugging)"},
    {"bind", 'b', "0.0.0.0", 0, "Listen on (ipv4 address)"},
    {"bind6", 0xfeff, "::", 0, "Listen on (ipv6 address)"},
    {"port", 'p', "5900", 0, "Listen port"},
    {"disable-ipv6", '4', 0, OPTION_ARG_OPTIONAL, "Disable ipv6"},
    {"fps", 0xff00, "30", 0, "Target frames per second"},
    {"disable-always-shared", 0xff01, 0, OPTION_ARG_OPTIONAL, "Do not always treat incoming connections as shared"},
    {"disable-compare-fb", 0xff02, 0, OPTION_ARG_OPTIONAL, "Do not compare pixels"},
    {"capture-cursor", 'c', 0, OPTION_ARG_OPTIONAL, "Capture mouse cursor"},
    {"capture-raw-fb", 0xff03, "/tmp/rawfb.bin", 0, "Capture RAW framebuffer instead of starting the vnc server (for debugging)"},
    {"va-derive", 0xff04, "off", 0, "Enable derive with vaapi"},
    {"debug", 0xff05, 0, OPTION_ARG_OPTIONAL, "Print debug message"},
    {"input-width", 0xff06, "0", 0, "Explicitly set input width, normally this is inferred from screen width on a single display system"},
    {"input-height", 0xff07, "0", 0, "Explicitly set input height"},
    {"input-offx", 0xff08, "0", 0, "Set input offset of x axis on a multi display system"},
    {"input-offy", 0xff09, "0", 0, "Set input offset of y axis on a multi display system"},
    {"screen-blank", 0xff0a, 0, OPTION_ARG_OPTIONAL, "Blank screen with gamma set on crtc"},
    {"screen-blank-restore-linear", 0xff0b, 0, OPTION_ARG_OPTIONAL, "Restore linear values on exit in case of messed up gamma"},
    {"trust-va-format", 0xff0c, 0, OPTION_ARG_OPTIONAL, "trust VAImageFormat returned by vaapi implementation unconditionally"},
    {"wakeup", 'w', 0, OPTION_ARG_OPTIONAL, "Move mouse to wake the system up before start"},
    {"disable-input", 'i', 0, OPTION_ARG_OPTIONAL, "Disable uinput"},
    {"desktop-name", 'n', "kmsvnc", 0, "Specify vnc desktop name"},
    {0}
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    int *arg_cout = state->input;

    switch (key) {
        case 'd':
            kmsvnc->card = arg;
            break;
        case 0xfefc:
            kmsvnc->source_plane = atoi(arg);
            break;
        case 0xfefd:
            kmsvnc->source_crtc = atoi(arg);
            break;
        case 0xfefe:
            kmsvnc->force_driver = arg;
            break;
        case 'b':
            if (!inet_aton(arg, kmsvnc->vnc_opt->bind)) {
                argp_error(state, "invalid ipv4 address %s", arg);
            }
            break;
        case 0xfeff:
            kmsvnc->vnc_opt->bind6 = arg;
            break;
        case 'p':
            {
                int port = atoi(arg);
                if (port > 0 && port < 65536) {
                    kmsvnc->vnc_opt->port = port;
                }
                else {
                    argp_error(state, "invalid port %s", arg);
                }
            }
            break;
        case '4':
            kmsvnc->vnc_opt->disable_ipv6 = 1;
            break;
        case 0xff00:
            {
                int fps = atoi(arg);
                if (fps > 0 && fps < 1000) {
                    kmsvnc->vnc_opt->sleep_ns = NS_IN_S / fps;
                }
                else {
                    argp_error(state, "invalid fps %s", arg);
                }
            }
            break;
        case 0xff01:
            kmsvnc->vnc_opt->always_shared = 0;
            break;
        case 0xff02:
            kmsvnc->vnc_opt->disable_cmpfb = 1;
            break;
        case 'c':
            kmsvnc->capture_cursor = 1;
            break;
        case 0xff03:
            kmsvnc->debug_capture_fb = arg;
            kmsvnc->disable_input = 1;
            break;
        case 0xff04:
            if (!strcmp("on", arg) || !strcmp("y", arg) || !strcmp("yes", arg) || !strcmp("1", arg)) {
                kmsvnc->va_derive_enabled = 1;
            }
            else {
                kmsvnc->va_derive_enabled = 0;
            }
            break;
        case 0xff05:
            kmsvnc->debug_enabled = 1;
            break;
        case 0xff06:
            {
                int width = atoi(arg);
                if (width > 0) {
                    kmsvnc->input_width = width;
                }
            }
            break;
        case 0xff07:
            {
                int height = atoi(arg);
                if (height > 0) {
                    kmsvnc->input_height = height;
                }
            }
            break;
        case 0xff08:
            {
                int offset_x = atoi(arg);
                if (offset_x > 0) {
                    kmsvnc->input_offx = offset_x;
                }
            }
            break;
        case 0xff09:
            {
                int offset_y = atoi(arg);
                if (offset_y > 0) {
                    kmsvnc->input_offy = offset_y;
                }
            }
            break;
        case 0xff0a:
            kmsvnc->screen_blank = 1;
            break;
        case 0xff0b:
            kmsvnc->screen_blank_restore = 1;
            break;
        case 0xff0c:
            kmsvnc->trust_va_format = 1;
            break;
        case 'w':
            kmsvnc->input_wakeup = 1;
            break;
        case 'i':
            kmsvnc->disable_input = 1;
            break;
        case 'n':
            kmsvnc->vnc_opt->desktop_name = arg;
            break;
        case ARGP_KEY_ARG:
            return ARGP_ERR_UNKNOWN;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

int main(int argc, char **argv)
{
    kmsvnc = malloc(sizeof(struct kmsvnc_data));
    if (!kmsvnc) KMSVNC_FATAL("memory allocation error at %s:%d\n", __FILE__, __LINE__);
    memset(kmsvnc, 0, sizeof(struct kmsvnc_data));

    struct vnc_opt *vncopt = malloc(sizeof(struct vnc_opt));
    if (!vncopt) {
        free(kmsvnc);
        KMSVNC_FATAL("memory allocation error at %s:%d\n", __FILE__, __LINE__);
    }
    memset(vncopt, 0, sizeof(struct vnc_opt));

    kmsvnc->vnc_opt = vncopt;

    kmsvnc->card = "/dev/dri/card0";
    kmsvnc->va_derive_enabled = -1;
    kmsvnc->vnc_opt->bind = &(struct in_addr){0};
    kmsvnc->vnc_opt->always_shared = 1;
    kmsvnc->vnc_opt->port = 5900;
    kmsvnc->vnc_opt->sleep_ns = NS_IN_S / 30;
    kmsvnc->vnc_opt->desktop_name = "kmsvnc";

    static char *args_doc = "";
    static char *doc = "kmsvnc -- vncserver for DRM/KMS capable GNU/Linux devices";

    struct argp argp = {kmsvnc_main_options, parse_opt, args_doc, doc};
    argp_parse(&argp, argc, argv, 0, 0, NULL);

    if (!kmsvnc->disable_input) {
        const char* XKB_DEFAULT_LAYOUT = getenv("XKB_DEFAULT_LAYOUT");
        if (!XKB_DEFAULT_LAYOUT || strcmp(XKB_DEFAULT_LAYOUT, "") == 0) {
            printf("No keyboard layout set from environment variables, use US layout by default\n");
            printf("See https://xkbcommon.org/doc/current/structxkb__rule__names.html\n");
            setenv("XKB_DEFAULT_LAYOUT", "us", 1);
        }

        if (xkb_init()) {
            cleanup();
            return 1;
        }
        if (uinput_init()) {
            cleanup();
            return 1;
        }
    }
    if (drm_open()) {
        cleanup();
        return 1;
    }

    if (kmsvnc->debug_capture_fb) {
        int wfd = open(kmsvnc->debug_capture_fb, O_WRONLY | O_CREAT, 00644);
        int max_size = 0;
        for (int i = 0; i < 4; i++) {
            int size = kmsvnc->drm->mfb->offsets[i] + kmsvnc->drm->mfb->height * kmsvnc->drm->mfb->pitches[i];
            if (size > max_size) max_size = size;
        }
        printf("attempt to write %d bytes\n", max_size);
        if (wfd > 0) {
            if (kmsvnc->va) {
                if (!kmsvnc->drm->mapped) kmsvnc->drm->mapped = malloc(max_size);
                if (!kmsvnc->drm->mapped) {
                    cleanup();
                    KMSVNC_FATAL("memory allocation error at %s:%d\n", __FILE__, __LINE__);
                }
                va_hwframe_to_vaapi(kmsvnc->drm->mapped);
            }
            KMSVNC_WRITE_MAY(wfd, kmsvnc->drm->mapped, (ssize_t)max_size);
            fsync(wfd);
            printf("wrote raw frame buffer to %s\n", kmsvnc->debug_capture_fb);
        }
        else {
            fprintf(stderr, "open file %s failed, %s\n", kmsvnc->debug_capture_fb, strerror(errno));
        }
        if (kmsvnc->screen_blank) {
            sigset_t signal_set;
            int sig;
            sigemptyset(&signal_set);
            signal(SIGHUP, &signal_handler_noop);
            signal(SIGINT, &signal_handler_noop);
            signal(SIGTERM, &signal_handler_noop);
            sigaddset(&signal_set, SIGHUP);
            sigaddset(&signal_set, SIGINT);
            sigaddset(&signal_set, SIGTERM);
            fprintf(stderr, "blanking screen...\n");
            sigwait(&signal_set, &sig);
            fprintf(stderr, "got sig %d\n", sig);
        }
        cleanup();
        return 0;
    }

    size_t buflen = kmsvnc->drm->mfb->width * kmsvnc->drm->mfb->height * BYTES_PER_PIXEL;
    kmsvnc->buf = malloc(buflen);
    if (!kmsvnc->buf) {
        cleanup();
        KMSVNC_FATAL("memory allocation error at %s:%d\n", __FILE__, __LINE__);
    }
    memset(kmsvnc->buf, 0, buflen);
    kmsvnc->buf1 = malloc(buflen);
    if (!kmsvnc->buf1) {
        cleanup();
        KMSVNC_FATAL("memory allocation error at %s:%d\n", __FILE__, __LINE__);
    }
    memset(kmsvnc->buf1, 0, buflen);

    signal(SIGHUP, &signal_handler);
    signal(SIGINT, &signal_handler);
    signal(SIGTERM, &signal_handler);

    kmsvnc->server = rfbGetScreen(0, NULL, kmsvnc->drm->mfb->width, kmsvnc->drm->mfb->height, 8, 3, 4);
    if (!kmsvnc->server) {
        cleanup();
        return 1;
    }
    kmsvnc->server->desktopName = kmsvnc->vnc_opt->desktop_name;
    kmsvnc->server->frameBuffer = kmsvnc->buf;
    kmsvnc->server->port = kmsvnc->vnc_opt->port;
    kmsvnc->server->listenInterface = kmsvnc->vnc_opt->bind->s_addr;
    kmsvnc->server->ipv6port = kmsvnc->vnc_opt->disable_ipv6 ? 0 : kmsvnc->vnc_opt->port;
    kmsvnc->server->listen6Interface = kmsvnc->vnc_opt->bind6;
    kmsvnc->server->alwaysShared = kmsvnc->vnc_opt->always_shared;
    if (!kmsvnc->disable_input) {
        kmsvnc->server->kbdAddEvent = rfb_key_hook;
        kmsvnc->server->ptrAddEvent = rfb_ptr_hook;
    }
    rfbInitServer(kmsvnc->server);
    rfbRunEventLoop(kmsvnc->server, -1, TRUE);
    int cursor_frame = 0;
    while (rfbIsActive(kmsvnc->server))
    {
        between_frames();
        if (kmsvnc->server->clientHead)
        {
            kmsvnc->drm->funcs->sync_start(kmsvnc->drm->prime_fd);
            kmsvnc->drm->funcs->convert(kmsvnc->drm->mapped, kmsvnc->drm->mfb->width, kmsvnc->drm->mfb->height, kmsvnc->buf1);
            kmsvnc->drm->funcs->sync_end(kmsvnc->drm->prime_fd);
            update_screen_buf(kmsvnc->buf, kmsvnc->buf1, kmsvnc->drm->mfb->width, kmsvnc->drm->mfb->height);
            if (kmsvnc->capture_cursor) {
                cursor_frame++;
                cursor_frame %= CURSOR_FRAMESKIP;
                if (!cursor_frame) {
                    char *data = NULL;
                    int width, height;
                    int err = drm_dump_cursor_plane(&data, &width, &height);
                    if (!err && data) {
                        update_vnc_cursor(data, width, height);
                    }
                }
            }
        }
    }
    cleanup();
    return 0;
}
