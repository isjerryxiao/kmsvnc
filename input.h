#pragma once

#include <rfb/rfb.h>

#include "kmsvnc.h"

#define UINPUT_ABS_MAX INT16_MAX
#define UINPUT_MAX_KEY 256

#define INP_IOCTL_MUST(...) do{ int e; if (e = ioctl(__VA_ARGS__)) KMSVNC_FATAL("uinput ioctl error %d on line %d\n", e, __LINE__); } while(0)
#define INP_IOCTL_MAY(...) do{ int e; if (e = ioctl(__VA_ARGS__)) fprintf(stderr, "uinput ioctl error %d on line %d\n", e, __LINE__); } while(0)

void uinput_cleanup();
int uinput_init();
void rfb_key_hook(rfbBool down, rfbKeySym keysym, rfbClientPtr cl);
void rfb_ptr_hook(int mask, int screen_x, int screen_y, rfbClientPtr cl);
