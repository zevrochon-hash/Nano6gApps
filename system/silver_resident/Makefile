# Resident Silver hook module — a relocatable .hbapp. The boot stub
# (system/disk_bootstrap) loads it into its own OS-heap block once per boot, so
# it owns no fixed DRAM (0x0910xxxx-0x094xxxxx is all live OS heap). Its .bss
# (trampolines, vtable copies, trace ring, shared app state) is part of that
# block; the stub zeroes it before calling payload_entry.
APP_NAME := silver_resident
SRCS     := silver_resident.c bluetooth_fix.c dock_fix.c ../../sdk/hb_app_catalog.c
# RELOC=1 (default): relocatable .hbapp, loaded from disk by the boot stub.
# RELOC=0 LINK_VA=0x... BSS_VA=0x...: fixed-VA image for one-shot SCSI push tests.
RELOC ?= 1
EXTRA_CFLAGS += -DHB_RAWMSG_TRACE
ICON_ONLY ?= 0
TRACE_ONLY ?= 0
NO_APPREG ?= 0
NO_REG ?= 0
ifeq ($(ICON_ONLY),1)
EXTRA_CFLAGS += -DHB_SILVER_ICON_ONLY=1
endif
ifeq ($(TRACE_ONLY),1)
EXTRA_CFLAGS += -DHB_SILVER_TRACE_ONLY=1 -DHB_SILVER_ICON_ONLY=1
endif
ifeq ($(NO_APPREG),1)
EXTRA_CFLAGS += -DHB_6G_NO_APPREG=1
endif
ifeq ($(NO_REG),1)
EXTRA_CFLAGS += -DHB_SILVER_NO_REG=1
endif
# 6G arena: the 7G default (4 MB) does not fit anywhere on the 6G — the whole
# 64 MB DRAM is busy except a handful of ~1 MB free holes (largest observed:
# 0x09350000-0x09480000). 1 MB still holds a small app pack + one app image +
# a few icons, and keeps the resident's whole span (~1.1 MB) inside one hole.
EXTRA_CFLAGS += -DHB_ARENA_BYTES=65536
include ../../sdk/hb_app.mk
