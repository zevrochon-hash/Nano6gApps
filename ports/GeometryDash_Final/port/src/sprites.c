#include "sprites.h"
#include "platform.h"

#define MAX_SPRITES 64

static struct { int id; int w; int h; } sprites[MAX_SPRITES];
static int nsprites = 0;

int sprites_load(void) {
    /* Minimal implementation - no sprite loading */
    nsprites = 0;
    return 0;
}

void sprites_forget(void) {
    nsprites = 0;
}

int spr_find(const char *name) {
    /* Return sprite ID by name - placeholder */
    (void)name;
    return 0;
}

int spr_w(int id) {
    /* Return sprite width - placeholder */
    (void)id;
    return 20;
}

int spr_h(int id) {
    /* Return sprite height - placeholder */
    (void)id;
    return 20;
}