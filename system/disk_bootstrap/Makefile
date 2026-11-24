# Stable, exact-bound stage-2 loader. The firmware cave loads this one small
# image; it waits for and verifies /Library/resident.hbapp.
APP_NAME := disk_bootstrap
SRCS     := disk_bootstrap.c
RELOC    := 1
# The earlier 7G-derived setting here was far too large and pushed the reloc
# image past the deferred-stage 16-bit immediate limits. On the current 6G
# build, disk_bootstrap itself only needs a small amount of temporary space,
# so keep this deliberately tiny unless a measured need appears.
EXTRA_CFLAGS += -DHB_ARENA_BYTES=16384
include ../../sdk/hb_app.mk
