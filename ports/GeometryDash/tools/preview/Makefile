# Offline preview: links the real game code against a software renderer.
# Run from this directory or via `make -C tools/preview`.

CC      ?= cc
CFLAGS  ?= -O2 -std=gnu99 -Wall
SRC_DIR := ../../port/src
DATA    := ../../data

PREVIEW_SRC := soft_gfx.c preview_main.c $(SRC_DIR)/game.c $(SRC_DIR)/sprites.c

.PHONY: all run shots sim clean

all: preview

preview: $(PREVIEW_SRC)
	$(CC) $(CFLAGS) -I$(SRC_DIR) $(PREVIEW_SRC) -lm -o $@

# Capture every screen into shots/
shots: preview
	rm -rf shots
	mkdir -p shots
	./preview $(DATA) shots
	@if command -v magick >/dev/null 2>&1; then \
		magick mogrify -format png shots/*.ppm; \
	elif command -v convert >/dev/null 2>&1; then \
		for f in shots/*.ppm; do convert "$$f" "$${f%.ppm}.png"; done; \
	else \
		echo "PPM files written to shots/ (install ImageMagick to convert to PNG)"; \
	fi

# Same scenes plus the 1800-frame headless gameplay/collision/save simulation.
sim: preview
	GD_SIM=1 ./preview $(DATA) /tmp/gd-preview-sim

clean:
	rm -f preview
	rm -rf shots
