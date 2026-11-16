# Stable, exact-bound stage-2 loader. The firmware cave loads this one small
# image; it waits for and verifies /Library/resident.hbapp.
APP_NAME := disk_bootstrap
SRCS     := disk_bootstrap.c
RELOC    := 1
# The bootstrap's arena is the one the resident's whole span is allocated from,
# so it must stay LARGER than the resident's span. The 6G resident's .bss is
# ~77 KB (~146 KB span), so 256 KB matches the 6G budget much better than the
# previous 7G-derived 6 MB shim.
EXTRA_CFLAGS += -DHB_ARENA_BYTES=16384
include ../../sdk/hb_app.mk
