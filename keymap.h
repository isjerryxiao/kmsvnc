#pragma once

#include "kmsvnc.h"

#define XKB_FATAL(...) { fprintf(stderr, __VA_ARGS__); return 1; }

void xkb_cleanup();
int xkb_init();
void key_iter(struct xkb_keymap *xkb, xkb_keycode_t key, void *data);
