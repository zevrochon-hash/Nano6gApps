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
# 6G arena: this is now intentionally tiny because hb_heap.c uses the 6G OS
# allocator directly instead of reserving a large static arena in .bss. Keep it
# small unless a specific resident-side need is measured and justified.
EXTRA_CFLAGS += -DHB_ARENA_BYTES=8192
# 6G RAM pack stage (no OS fs port yet). This is a boot-specific free hole in
# the live OS heap; pick one by scanning the device before a push. Default is
# the last observed hole and is only a fallback.
PACK_STAGE ?= 0x09490000
EXTRA_CFLAGS += -DHB_6G_PACK_STAGE=$(PACK_STAGE)u
# 6G home-item descriptor slots that carry the app icon (sdk/hb_silver_res.c).
# 3 = stock primary icon slot (105x105) only; 1 = the other 105x105 slot;
# 2 = the first 164x154 overlay slot; 4 = all four (debug A/B).
ITEM_ICON_SLOTS ?= 1
EXTRA_CFLAGS += -DHB_6G_ITEM_ICON_SLOTS=$(ITEM_ICON_SLOTS)
# Diagnostic: give each test app a different descriptor slot ({3,1,2} by app
# index) so one boot reveals which slot the resting Home grid draws.
SLOT_PROBE ?= 0
ifeq ($(SLOT_PROBE),1)
EXTRA_CFLAGS += -DHB_6G_SLOT_PROBE=1
endif
include ../../sdk/hb_app.mk
