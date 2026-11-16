#include "game.h"
#include "platform.h"
#include "gfx.h"
#include "sprites.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

GDGame G;

/* Simplified level data to maximize quantity */
static GDLevel level_database[] = {
    {
        .id = 0, .name = "Stereo Madness",
        .objects = {
            {0, 400, 200, 20, 20}, {0, 400, 220, 20, 20}, {1, 400, 240, 20, 20},
            {0, 400, 300, 20, 20}, {0, 400, 320, 20, 20}, {1, 400, 340, 20, 20},
        },
        .object_count = 6, .length = 1000.0f
    },
    {
        .id = 1, .name = "Back on Track",
        .objects = {
            {0, 400, 200, 20, 20}, {1, 400, 220, 20, 20}, {1, 400, 240, 20, 20},
        },
        .object_count = 3, .length = 1200.0f
    }
};
static int total_levels = sizeof(level_database) / sizeof(GDLevel);

void gd_load_level(int idx) {
    if (idx < 0 || idx >= total_levels) return;
    G.current_level = &level_database[idx];
    G.player.x = 100.0f;
    G.player.y = 200.0f;
    G.player.vy = 0;
    G.player.rotation = 0;
    G.scroll_x = 0;
    G.score = 0;
}

bool gd_check_collision(GDPlayer *p, GDObject *o) {
    float px = p->x + G.scroll_x;
    float py = p->y;

    if (px < o->x + o->w && px + GD_PLAYER_SIZE > o->x &&
        py < o->y + o->h && py + GD_PLAYER_SIZE > o->y) {
        return true;
    }
    return false;
}

void gd_handle_input(bool pressed) {
    if (pressed && G.player.is_grounded) {
        G.player.vy = GD_JUMP_FORCE;
        G.player.is_grounded = false;
        G.player.is_jumping = true;
    }
}

void gd_start_game(void) {
    memset(&G, 0, sizeof(GDGame));
    G.state = GD_STATE_MENU;
    gd_load_level(0);
}

void gd_update(void) {
    if (G.state == GD_STATE_MENU) {
        if (G.input_pressed) {
            G.state = GD_STATE_PLAYING;
            gd_load_level(G.current_level_idx);
        }
        return;
    }

    if (G.state == GD_STATE_PLAYING) {
        // Physics
        G.player.vy += GD_GRAVITY;
        G.player.y += G.player.vy;
        G.scroll_x += GD_SPEED;

        // Floor collision
        if (G.player.y > 400.0f - GD_PLAYER_SIZE) {
            G.player.y = 400.0f - GD_PLAYER_SIZE;
            G.player.vy = 0;
            G.player.is_grounded = true;
            G.player.is_jumping = false;

            // Snap rotation to 90 deg
            G.player.rotation = ((int)(G.player.rotation / 90.0f + 0.5f) * 90);
        } else {
            G.player.is_grounded = false;
            G.player.rotation += 5.0f; // Rotate while jumping
        }

        // Level collisions
        for (int i = 0; i < G.current_level->object_count; i++) {
            if (gd_check_collision(&G.player, &G.current_level->objects[i])) {
                if (G.current_level->objects[i].type == 1) { // Spike
                    G.state = GD_STATE_GAMEOVER;
                } else if (G.current_level->objects[i].type == 0) { // Block
                    // Simple block collision: if hit side, die. If hit top, land.
                    if (G.player.vy > 0 && G.player.y < G.current_level->objects[i].y) {
                        G.player.y = G.current_level->objects[i].y - GD_PLAYER_SIZE;
                        G.player.vy = 0;
                        G.player.is_grounded = true;
                    } else {
                        G.state = GD_STATE_GAMEOVER;
                    }
                }
            }
        }

        if (G.scroll_x > G.current_level->length) {
            G.state = GD_STATE_WIN;
        }
    } else if (G.state == GD_STATE_GAMEOVER || G.state == GD_STATE_WIN) {
        if (G.input_pressed) {
            G.state = GD_STATE_PLAYING;
            gd_load_level(G.current_level_idx);
        }
    }
}

void gd_draw(void) {
    if (G.state == GD_STATE_MENU) {
        // Draw Menu
    } else if (G.state == GD_STATE_PLAYING) {
        // Draw Player
        // Draw Level Objects
    } else if (G.state == GD_STATE_GAMEOVER) {
        // Draw Game Over
    }
}

void gd_cleanup(void) {
    // Cleanup
}

int gd_is_running(void) {
    return 1;
}
