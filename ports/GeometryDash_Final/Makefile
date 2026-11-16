APP_NAME := geodash
ENTRY := gd_entry
NANOAPPS := /home/zev/Desktop/iPod/Build/NanoApps
SRCS := port/nano/app/$(ENTRY).c \
        port/src/game.c \
        port/src/gfx.c \
        port/src/sprites.c \
        port/nano/nano_platform.c \
        port/nano/nano_alloc.c \
        port/nano/nano_libc.c \
        port/nano/nano_memory.c
EXTRA_CFLAGS += -I/home/zev/Desktop/iPod/Build/NanoApps/ports/GeometryDash_Final/port/src \
                -I/home/zev/Desktop/iPod/Build/NanoApps/ports/GeometryDash_Final/port/nano
include $(NANOAPPS)/sdk/hb_app.mk
