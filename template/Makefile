# SPDX-License-Identifier: GPL-3.0-only
# hello-az: build the mod inside the SDK container.
#
#   <sdk>/tools/azmod-pack build .   builds in the container (recommended)
#   make                             only inside the container (or another aarch64
#                                    Ubuntu 18.04 toolchain); azmod-pack runs this
#   make DEBUG=1 OUT=out-debug       a copy with debug info (-g) for addr2line: the same code
#                                    at the same addresses as the plain build, plus line tables
#   make test [SAN=plain|asan|tsan]  on your computer, with clang: build the mod for the
#                                    host and run it under the SDK's test harness, plain,
#                                    with AddressSanitizer + UBSan, or with ThreadSanitizer
#                                    (default: all three). AZ_SDK is the SDK checkout.
#
# The flags below are the rules from docs/GUIDELINES.md in Makefile form:
#   -fvisibility=hidden          export only az_mod_v1_entry (rule: one symbol)
#   -fPIC -shared                a shared library the core can dlopen
#   -static-libgcc               no libgcc_s dependency
#   -Wl,--no-undefined           a missing function fails the build, not the deck
#   -Werror=implicit-function-declaration   same reason, at compile time
#   -fno-omit-frame-pointer     if your mod ever crashes the deck, the core can trace it
#                                back to your mod (and only your mod is switched off)
#   -fexceptions -fasynchronous-unwind-tables   the deck's own code throws C++
#                                exceptions through call chains your code can sit in;
#                                without unwind tables that becomes a crash
#   -mno-outline-atomics        (only when the compiler knows it) keeps atomics inline
#
# C++: list .cpp or .cc files in SRCS (C and C++ can be mixed). They compile with $(CXX)
# and the library links with it, against the deck's own libstdc++.so.6 and libgcc_s.so.1
# (one C++ runtime and one unwinder in EP147, shared with its own code), and with a version
# script that exports only az_mod_v1_entry: C++ templates from the standard headers are
# otherwise exported, which the deck refuses. A C-only build is unchanged by any of this.

MOD_ID          := hello-az
SRCS            := hello-az.c
AZ_SDK_INCLUDE  ?= ../include
AZ_SDK          ?= ..
MAX_REDRAWS     ?= 4          # the most repaints a second your tick asks for
SAN             ?= plain asan tsan
TEST_CC         ?= clang
TEST_CXX        ?= clang++
OUT             ?= out
DEBUG           ?= 0          # 1: add -g (debug info only; the code is the same)

# make's built-in default is cc; use gcc unless CC was given on the command line
ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS  := -O2 -Wall -Wextra -Werror=implicit-function-declaration \
           -fPIC -fvisibility=hidden -fno-omit-frame-pointer \
           -fexceptions -fasynchronous-unwind-tables \
           -I$(AZ_SDK_INCLUDE)
LDFLAGS := -shared -static-libgcc -Wl,--no-undefined -Wl,--hash-style=gnu
LDLIBS  :=        # add -lpthread if you start a thread, -lm for math functions

# GCC 10+ on aarch64 defaults to outline atomics (a libgcc helper). The SDK's GCC 7
# does not know the flag; probe for it so the same Makefile works on both.
NO_OUTLINE_ATOMICS := $(shell printf 'int main(void){return 0;}\n' | \
    $(CC) -mno-outline-atomics -x c -o /dev/null - 2>/dev/null && echo -mno-outline-atomics)
CFLAGS += $(NO_OUTLINE_ATOMICS)

CXX_SRCS   := $(filter %.cpp %.cc,$(SRCS))
CXXFLAGS   := -O2 -Wall -Wextra -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
              -fno-omit-frame-pointer -fexceptions -fasynchronous-unwind-tables \
              -I$(AZ_SDK_INCLUDE) $(NO_OUTLINE_ATOMICS)
CXXLDFLAGS := -shared -Wl,--no-undefined -Wl,--hash-style=gnu \
              -Wl,--version-script=$(OUT)/exports.map
ifeq ($(strip $(DEBUG)),1)
CFLAGS   += -g
CXXFLAGS += -g
endif

comma := ,
space := $(subst ,, )

.PHONY: all clean test
all: $(OUT)/$(MOD_ID).so

ifeq ($(strip $(CXX_SRCS)),)
$(OUT)/$(MOD_ID).so: $(SRCS) $(AZ_SDK_INCLUDE)/azmod-mod.h | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LDLIBS)
else
OBJS := $(patsubst %,$(OUT)/obj/%.o,$(SRCS))
$(OUT)/obj/%.c.o: %.c $(AZ_SDK_INCLUDE)/azmod-mod.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OUT)/obj/%.cpp.o: %.cpp $(AZ_SDK_INCLUDE)/azmod-mod.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
$(OUT)/obj/%.cc.o: %.cc $(AZ_SDK_INCLUDE)/azmod-mod.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
$(OUT)/exports.map: | $(OUT)
	printf '{ global: az_mod_v1_entry; local: *; };\n' > $@
$(OUT)/$(MOD_ID).so: $(OBJS) $(OUT)/exports.map
	$(CXX) $(CXXLDFLAGS) -o $@ $(OBJS) $(LDLIBS)
endif

$(OUT):
	mkdir -p $(OUT)

test:
	@test -f $(AZ_SDK)/tools/harness/sanitize.sh || \
	    { echo "make test: set AZ_SDK to your az-mods-sdk checkout" >&2; exit 2; }
	CC=$(TEST_CC) CXX=$(TEST_CXX) sh $(AZ_SDK)/tools/harness/sanitize.sh --passes "$(SAN)" \
	    $(MOD_ID):$(strip $(MAX_REDRAWS)):$(subst $(space),$(comma),$(strip $(SRCS)))

clean:
	rm -rf $(OUT)
