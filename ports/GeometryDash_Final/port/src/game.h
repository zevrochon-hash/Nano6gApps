#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include <stdbool.h>

/* Game Constants */
#define GD_GRAVITY -0.4f
#define GD_JUMP_FORCE 8.0f
#define GD_PLAYER_SIZE 20
#define GD_SPEED 4.0f
#define MAX_LEVEL_OBJECTS 1024

typedef enum {
    GD_STATE_MENU,
    GD_STATE_PLAYING,
    GD_STATE_GAMEOVER,
    GD_STATE_WIN
} GDGameState;

typedef struct {
    float x, y;
    float vy;
    bool is_grounded;
    bool is_jumping;
    float rotation;
} GDPlayer;

typedef struct {
    int type; // 0: Block, 1: Spike, 2: Portal
    int x, y;
    int w, h;
} GDObject;

typedef struct {
    int id;
    const char* name;
    GDObject objects[MAX_LEVEL_OBJECTS];
    int object_count;
    float length;
} GDLevel;

typedef struct {
    GDGameState state;
    GDPlayer player;
    GDLevel *current_level;
    int current_level_idx;
    float scroll_x;
    int score;
    int best_score;
    bool input_pressed;
} GDGame;

extern GDGame G;

void gd_start_game(void);
void gd_update(void);
void gd_draw(void);
void gd_cleanup(void);
int gd_is_running(void);

/* Internal logic */
void gd_load_level(int idx);
bool gd_check_collision(GDPlayer *p, GDObject *o);
void gd_handle_input(bool pressed);

#endif