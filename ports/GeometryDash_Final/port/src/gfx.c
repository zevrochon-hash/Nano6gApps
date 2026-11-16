#include "gfx.h"
#include "platform.h"
#include "game.h"
#include "sprites.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void gfx_init(void) {
    /* Initialize display and graphics system */
}

void gfx_shutdown(void) {
    /* Shutdown graphics system */
}

void gfx_loop(void) {
    /* Main game loop */
}

void gfx_clear(unsigned int color) {
    /* Clear the nano screen with the specified color */
}

void gfx_set_title(const char *title) {
    (void)title;
}

void gfx_set_frame_offset(float x, float y) {
    (void)x; (void)y;
}

void gd_render_player(GDPlayer *p) {
    int sprite = spr_find("player_cube");
    if (sprite >= 0) {
        /* Actual SDK call to render the rotated sprite */
    }
}

void gd_render_level(GDLevel *lvl, float scroll_x) {
    for (int i = 0; i < lvl->object_count; i++) {
        GDObject *o = &lvl->objects[i];
        int sprite = (o->type == 0) ? spr_find("block") : spr_find("spike");
        if (sprite >= 0) {
            /* Actual SDK call to render the sprite at (o->x - scroll_x, o->y) */
        }
    }
}

void gd_render_ui(GDGameState state) {
    if (state == GD_STATE_MENU) {
        /* Render Menu */
    } else if (state == GD_STATE_GAMEOVER) {
        /* Render Game Over */
    } else if (state == GD_STATE_WIN) {
        /* Render Win screen */
    }
}

void gd_draw_full(void) {
    gfx_clear(0xFF0000FF); // Original Blue Background

    if (G.state == GD_STATE_PLAYING) {
        gd_render_level(G.current_level, G.scroll_x);
        gd_render_player(&G.player);
    }

    gd_render_ui(G.state);
}
