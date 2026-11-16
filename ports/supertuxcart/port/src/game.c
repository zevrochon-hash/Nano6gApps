#include "game.h"
#include "gfx.h"
#include "hb_sdk.h"

STCMap maps[STC_MAX_MAPS];
int cur_map;
STCCart cart;
STCObstacle obstacles[8];

#define ROAD_LEFT 42
#define ROAD_RIGHT 278
static int initialized;
static unsigned rng = 0x53544331u;
static int road_scroll, flash_frames;

static unsigned next_random(void) { rng = rng * 1664525u + 1013904223u; return rng; }
static void rect(float x, float y, float w, float h, uint32_t color) {
    float xy[8] = { x,y, x+w,y, x+w,y+h, x,y+h }; float uv[8] = { 0 };
    gfx_set_blend(GFX_BLEND_NONE); gfx_quad(0, 1, 1, xy, uv, color);
}
static void reset_race(void) {
    cart.x=145.f; cart.y=177.f; cart.dx=cart.dy=cart.angle=0.f;
    cart.lives=3; cart.score=cur_map=road_scroll=flash_frames=0;
    for (int i=0;i<8;i++) obstacles[i].active=0;
}
void stc_init(void) {
    for (int m=0;m<STC_MAX_MAPS;m++) { maps[m].type=m; maps[m].passed=0; maps[m].x=maps[m].y=0; }
    reset_race(); initialized=1;
}
void stc_frame(int w, int h, uint32_t frame) {
    (void)w; (void)h; if (!initialized) stc_init();
    int32_t accel[3]; hb_accel_read_milli_g(accel);
    /* On the rotated landscape canvas physical side-to-side tilt is the Nano's Y axis. */
    int tilt=accel[1]; if (tilt>-45 && tilt<45) tilt=0;
    cart.dx += -(float)tilt / 420.f * .34f; cart.dx *= .90f;
    if (cart.dx>5.4f) cart.dx=5.4f;
    if (cart.dx<-5.4f) cart.dx=-5.4f;
    cart.x += cart.dx; cart.angle=cart.dx*1.8f;
    if (cart.x<ROAD_LEFT+4) { cart.x=ROAD_LEFT+4; cart.dx=0; }
    if (cart.x>ROAD_RIGHT-34) { cart.x=ROAD_RIGHT-34; cart.dx=0; }
    road_scroll=(road_scroll+5)%40; if (flash_frames) --flash_frames;
    if (!(frame%42u)) for (int i=0;i<8;i++) if (!obstacles[i].active) {
        obstacles[i].active=1; obstacles[i].x=(float)(ROAD_LEFT+8+next_random()%202u); obstacles[i].y=-26.f;
        obstacles[i].dx=(float)((int)(next_random()%3u)-1)*.45f; obstacles[i].dy=3.1f+(float)(cur_map%3)*.25f; break;
    }
    for (int i=0;i<8;i++) if (obstacles[i].active) {
        STCObstacle *o=&obstacles[i]; o->x+=o->dx; o->y+=o->dy;
        if (o->x<ROAD_LEFT+3 || o->x>ROAD_RIGHT-27) o->dx=-o->dx;
        if (o->y>245) { o->active=0; ++cart.score; continue; }
        if (o->x<cart.x+26 && o->x+24>cart.x && o->y<cart.y+38 && o->y+20>cart.y) {
            o->active=0; --cart.lives; flash_frames=18; if (cart.lives<=0) reset_race();
        }
    }
    if (cart.score && !(cart.score%12)) cur_map=(cart.score/12)%STC_MAX_MAPS;
}
void stc_draw(void) {
    rect(0,0,320,240,flash_frames?0xff442424u:0xff23633au); rect(34,0,252,240,0xffd6c778u); rect(42,0,236,240,0xff34373du);
    for (int y=-40+road_scroll;y<240;y+=40) rect(157,(float)y,6,20,0xfff8f1c8u);
    for (int y=0;y<240;y+=24) { rect(42,(float)y,4,12,0xffffffffu); rect(274,(float)y,4,12,0xffffffffu); }
    for (int i=0;i<8;i++) if (obstacles[i].active) { STCObstacle *o=&obstacles[i]; rect(o->x+3,o->y+2,18,18,0xff741d2fu); rect(o->x,o->y+6,24,10,0xffd64046u); rect(o->x+5,o->y+4,14,6,0xffa9e3f2u); }
    rect(cart.x+1,cart.y+27,7,12,0xff101014u); rect(cart.x+24,cart.y+27,7,12,0xff101014u); rect(cart.x+3,cart.y+13,27,18,0xff2782d0u);
    rect(cart.x+7,cart.y+7,19,13,0xff15161bu); rect(cart.x+10,cart.y+10,13,7,0xfff5f5f5u); rect(cart.x+6,cart.y+29,21,7,0xffffc629u);
    rect(cart.x+8,cart.y+31,5,2,0xffffffffu); rect(cart.x+20,cart.y+31,5,2,0xffffffffu);
    for (int i=0;i<cart.lives;i++) rect(10+i*13,10,9,9,0xffffc629u);
    int bars=cart.score>20?20:cart.score; for (int i=0;i<bars;i++) rect(10+i*5,28,3,7,0xff8ce06bu);
    for (int i=0;i<=cur_map;i++) rect(286+i*6,10,4,20,0xff77c8ffu);
}
int stc_wants_exit(void) { return 0; }
