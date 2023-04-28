#pragma once

#include "kmsvnc.h"

void xkb_cleanup();
int xkb_init();
void key_iter(struct xkb_keymap *xkb, xkb_keycode_t key, void *data);
