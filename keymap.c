#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "keymap.h"

extern struct kmsvnc_data *kmsvnc;

void xkb_cleanup() {
    if (kmsvnc->keymap) {
        if (kmsvnc->keymap->map) {
            xkb_keymap_unref(kmsvnc->keymap->map);
            kmsvnc->keymap->map = NULL;
        }
        if (kmsvnc->keymap->ctx) {
            xkb_context_unref(kmsvnc->keymap->ctx);
            kmsvnc->keymap->ctx = NULL;
        }
        free(kmsvnc->keymap);
        kmsvnc->keymap = NULL;
    }
}

int xkb_init()
{
    struct kmsvnc_keymap_data *xkb = malloc(sizeof(struct kmsvnc_keymap_data));
    if (!xkb) KMSVNC_FATAL("memory allocation error at %s:%d\n", __FILE__, __LINE__);
    memset(xkb, 0, sizeof(struct kmsvnc_keymap_data));
    kmsvnc->keymap = xkb;

    xkb->ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (xkb->ctx == NULL)
    {
        KMSVNC_FATAL("Failed to create XKB context\n");
    }
    struct xkb_rule_names names = {
        .rules = NULL,
        .model = NULL,
        .layout = NULL,
        .variant = NULL,
        .options = NULL,
    };
    xkb->map = xkb_keymap_new_from_names(xkb->ctx, &names, 0);
    if (xkb->map == NULL)
    {
        KMSVNC_FATAL("Failed to create XKB keymap\n");
    }
    // printf("xkb: keymap string\n%s\n", xkb_keymap_get_as_string(xkb->map, XKB_KEYMAP_USE_ORIGINAL_FORMAT));
    return 0;
}


void key_iter(struct xkb_keymap *xkb, xkb_keycode_t key, void *data)
{
    struct key_iter_search *search = data;
    if (search->keycode != XKB_KEYCODE_INVALID)
    {
        return; // We are done
    }
    xkb_level_index_t num_levels = xkb_keymap_num_levels_for_key(xkb, key, 0);
    for (xkb_level_index_t i = 0; i < num_levels; i++)
    {
        const xkb_keysym_t *syms;
        int num_syms = xkb_keymap_key_get_syms_by_level(xkb, key, 0, i, &syms);
        for (int k = 0; k < num_syms; k++)
        {
            if (syms[k] == search->keysym)
            {
                search->keycode = key;
                search->level = i;
                goto end;
            }
        }
    }
end:
    return;
}
