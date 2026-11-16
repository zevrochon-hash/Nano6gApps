#ifndef STC_GAME_H
#define STC_GAME_H
#include <stdint.h>
#include <stdbool.h>
#define STC_MAX_MAPS 5
#define STC_MAP_W 240
#define STC_MAP_H 288
typedef struct { int x, y, w, h; } STCRect;
typedef struct { float x, y, dx, dy, angle; int lives; int score; } STCCart;
typedef struct { float x, y, dx, dy; int active; } STCObstacle;
typedef struct { float x, y; int type; int passed; } STCMap;
extern STCMap maps[STC_MAX_MAPS];
extern int cur_map;
extern STCCart cart;
extern STCObstacle obstacles[8];
void stc_init(void);
void stc_frame(int w, int h, uint32_t frame);
void stc_draw(void);
int stc_wants_exit(void);
#endif