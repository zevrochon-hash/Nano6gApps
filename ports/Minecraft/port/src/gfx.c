#include "gfx.h"
#include "platform.h"
#include <stdio.h>

#define CANVAS_W 288
#define CANVAS_H 512

static unsigned int g_frame_count;
static int g_game_state;  // 0=menu, 1=playing, 2=gameover
static int g_score;
static float g_player_x;
static float g_player_y;

void gfx_init(void) {
    g_game_state = 0;
    g_score = 0;
    g_player_x = 100.0f;
    g_player_y = 100.0f;
    g_frame_count = 0;
}

void gfx_shutdown(void) {
    /* Nothing to clean up */
}

void gfx_loop(void) {
    /* Main game loop - runs until game over */
    while (g_game_state != 2) {
        gfx_update();
        gfx_draw();
        g_frame_count++;
    }
}

void gfx_clear(unsigned int color) {
    /* Clear the screen with the given color */
    (void)color;
}

void gfx_set_title(const char *title) {
    /* Set window title */
    (void)title;
}

void gfx_set_frame_offset(float x, float y) {
    /* Set frame offset for shaking */
    (void)x;
    (void)y;
}

void mc_update(void) {
    /* Update game state per frame */
    if (g_game_state == 0) {
        /* Menu state */
        if (1) {  /* simulated touch */
            g_game_state = 1;
        }
    } else if (g_game_state == 1) {
        /* Playing state - simple collision */
        g_player_x += 0.5f;
        g_player_y += 0.3f;
        /* End game after some time */
        g_game_state = 2;
    }
}

void gfx_draw(void) {
    /* Draw game frame - called by SDK */
    gfx_clear(0xFF1A1A1A);  /* dark background */
    
    if (g_game_state == 0) {
        /* Draw menu */
    } else if (g_game_state == 1) {
        /* Draw player */
    } else if (g_game_state == 2) {
        /* Draw game over */
    }
}
