# Stable, exact-bound stage-2 loader. The firmware cave loads this one small
# image; it waits for and verifies /Library/resident.hbapp.
APP_NAME := disk_bootstrap
SRCS     := disk_bootstrap.c
RELOC    := 1
# The bootstrap's arena is the one the resident's whole span is allocated from,
# so it must stay LARGER than the resident span (resident arena 4 MB + bss).
EXTRA_CFLAGS += -DHB_ARENA_BYTES=6291456
include ../../sdk/hb_app.mk
