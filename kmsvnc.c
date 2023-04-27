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
    if (width % 2 == 0) {
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
    max_x = max_x < 0 ? 0 : max_x;
    max_y = max_y < 0 ? 0 : max_y;
    min_x = min_x > width ? 0 : min_x;
    min_y = min_y > height ? 0 : min_y;

    //printf("dirty: %d, %d, %d, %d\n", min_x, min_y, max_x, max_y);
    if (max_x || max_y || min_x || min_y) {
        memcpy(to, from, width * height * BYTES_PER_PIXEL);
        rfbMarkRectAsModified(kmsvnc->server, min_x, min_y, max_x, max_y);
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
        free(kmsvnc);
        kmsvnc = NULL;
    }
}

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
    {"force-driver", 0xfefe, "i915", 0, "force a certain driver (for debug)"},
    {"bind", 'b', "0.0.0.0", 0, "Listen on (ipv4 address)"},
    {"bind6", 0xfeff, "::", 0, "Listen on (ipv6 address)"},
    {"port", 'p', "5900", 0, "Listen port"},
    {"disable-ipv6", '4', 0, OPTION_ARG_OPTIONAL, "Disable ipv6"},
    {"fps", 0xff00, "30", 0, "Target frames per second"},
    {"disable-always-shared", 0xff01, 0, OPTION_ARG_OPTIONAL, "Do not always treat incoming connections as shared"},
    {"disable-input", 'i', 0, OPTION_ARG_OPTIONAL, "Disable uinput"},
    {"desktop-name", 'n', "kmsvnc", 0, "Specify vnc desktop name"},
    {0}
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    int *arg_cout = state->input;

    switch (key) {
        case 'd':
            argp_usage(state);
            kmsvnc->card = arg;
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
            int port = atoi(arg);
            if (port > 0 && port < 65536) {
                kmsvnc->vnc_opt->port = port;
            }
            else {
                argp_error(state, "invalid port %s", arg);
            }
            break;
        case '4':
            kmsvnc->vnc_opt->disable_ipv6 = 1;
            break;
        case 0xff00:
            int fps = atoi(arg);
            if (fps > 0 && fps < 1000) {
                kmsvnc->vnc_opt->sleep_ns = NS_IN_S / fps;
            }
            else {
                argp_error(state, "invalid fps %s", arg);
            }
            break;
        case 0xff01:
            kmsvnc->vnc_opt->always_shared = 0;
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
    struct vnc_opt *vncopt = malloc(sizeof(struct vnc_opt));
    memset(vncopt, 0, sizeof(struct vnc_opt));

    kmsvnc = malloc(sizeof(struct kmsvnc_data));
    memset(kmsvnc, 0, sizeof(struct kmsvnc_data));

    kmsvnc->vnc_opt = vncopt;

    kmsvnc->card = "/dev/dri/card0";
    kmsvnc->vnc_opt->bind = &(struct in_addr){0};
    kmsvnc->vnc_opt->always_shared = 1;
    kmsvnc->vnc_opt->port = 5900;
    kmsvnc->vnc_opt->sleep_ns = NS_IN_S / 30;
    kmsvnc->vnc_opt->desktop_name = "kmsvnc";

    static char *args_doc = "";
    static char *doc = "kmsvnc -- vncserver for DRM/KMS capable GNU/Linux devices";

    struct argp argp = {kmsvnc_main_options, parse_opt, args_doc, doc};
    argp_parse(&argp, argc, argv, 0, 0, NULL);

    const char* XKB_DEFAULT_LAYOUT = getenv("XKB_DEFAULT_LAYOUT");
    if (!XKB_DEFAULT_LAYOUT || strcmp(XKB_DEFAULT_LAYOUT, "") == 0) {
        printf("No keyboard layout set from environment variables, use US layout by default\n");
        printf("See https://xkbcommon.org/doc/current/structxkb__rule__names.html\n");
        setenv("XKB_DEFAULT_LAYOUT", "us", 1);
    }

    if (!kmsvnc->disable_input) {
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

    size_t buflen = kmsvnc->drm->mfb->width * kmsvnc->drm->mfb->height * BYTES_PER_PIXEL;
    kmsvnc->buf = malloc(buflen);
    memset(kmsvnc->buf, 0, buflen);
    kmsvnc->buf1 = malloc(buflen);
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
    while (rfbIsActive(kmsvnc->server))
    {
        between_frames();
        if (kmsvnc->server->clientHead)
        {
            kmsvnc->drm->funcs->sync_start(kmsvnc->drm->prime_fd);
            kmsvnc->drm->funcs->convert(kmsvnc->drm->mapped, kmsvnc->drm->mfb->width, kmsvnc->drm->mfb->height, kmsvnc->buf1);
            kmsvnc->drm->funcs->sync_end(kmsvnc->drm->prime_fd);
            update_screen_buf(kmsvnc->buf, kmsvnc->buf1, kmsvnc->drm->mfb->width, kmsvnc->drm->mfb->height);
        }
    }
    cleanup();
    return 0;
}
