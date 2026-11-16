CC := cc
CFLAGS := -O2 -ffp-contract=off -g -std=c99 -Wall -Wextra -Wno-deprecated-declarations -MMD -MP -Isrc -Ihost
OBJS := build/host/gfx.o build/host/game.o build/host/host_main.o
all: build/mc
build/mc: $(OBJS)
	$(CC) -o $@ $(OBJS) -framework OpenGL -framework GLUT -framework OpenAL -lz
build/host/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
build/host/%.o: host/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
-include $(OBJS:.o=.d)
.PHONY: all