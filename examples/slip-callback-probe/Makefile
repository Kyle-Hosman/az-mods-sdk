# SPDX-License-Identifier: GPL-3.0-only
MOD_ID := sdk-slip-capture
AZ_SDK_INCLUDE ?= ../../include
OUT ?= out
ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror -fPIC -fvisibility=hidden \
 -fno-omit-frame-pointer -fno-optimize-sibling-calls -fexceptions -fasynchronous-unwind-tables -I$(AZ_SDK_INCLUDE)
SRCS := probe.c capture.c native/azmod/sdk_cache_entry.S
.PHONY: all
all: $(OUT)/$(MOD_ID).so
$(OUT)/$(MOD_ID).so: $(SRCS) capture.h binding.h profiles.h native/common/azcacheentry.h native/common/azsha256.h $(AZ_SDK_INCLUDE)/azmod-mod.h $(AZ_SDK_INCLUDE)/azmod-timestamps.h
	@mkdir -p $(OUT)
	printf '{ global: az_mod_v1_entry; local: *; };\n' > $(OUT)/exports.map
	$(CC) $(CFLAGS) -shared -static-libgcc -Wl,--no-undefined -Wl,--hash-style=gnu -Wl,--version-script=$(OUT)/exports.map -o $@ $(SRCS) -ldl -lpthread
