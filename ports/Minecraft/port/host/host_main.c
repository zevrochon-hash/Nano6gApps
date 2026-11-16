#include "platform.h"

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    gfx_init();
    gfx_set_title("Minecraft");
    gfx_loop();
    gfx_shutdown();
    return 0;
}