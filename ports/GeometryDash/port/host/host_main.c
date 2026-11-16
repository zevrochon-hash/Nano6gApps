#include "platform.h"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    /* Initialize game */
    game_init(288.0f, 512.0f);
    
    /* Seed the random number generator */
    uint32_t seed = plat_seed();
    
    /* Main loop */
    while (!game_wants_exit()) {
        /* Read touch input */
        /* In real implementation, get touches from iPod hardware */
        const void *touches = NULL;
        
        /* Frame update */
        game_frame(1.0f / 60.0f, touches);
        
        /* Render frame */
        game_redraw();
    }
    
    /* Cleanup */
    game_gl_lost();
    
    return 0;
}