# SPDX-License-Identifier: GPL-3.0-only
# sdlagent — Makefile
#
# Prerequisites (MSYS2 / macOS / Linux):
#   SDL3, md4c, cJSON, libcurl, nlohmann-json, a C++20 compiler, pkg-config
#
# Usage:
#   make            build ./SDLAgent (or SDLAgent.exe)
#   make debug      build with debug info and no optimization
#   make release    clean build with LTO, section GC and stripping
#   make run        build and run
#   make clean      remove build artifacts

ifeq ($(OS),Windows_NT)
TARGET := SDLAgent.exe
EMBED_TOOL := build/embed_prompts.exe
EMBED_BIN_TOOL := build/embed_binary.exe
else
TARGET := SDLAgent
EMBED_TOOL := build/embed_prompts
EMBED_BIN_TOOL := build/embed_binary
endif

CXX      ?= g++
CC       ?= gcc
PKGS     := sdl3 sdl3-ttf md4c libcjson libcurl

PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS))
# pkg-config for SDL3 adds -mwindows on Windows. We keep a console so that
# stderr logging stays visible during development.
PKG_LIBS   := $(filter-out -mwindows,$(shell pkg-config --libs $(PKGS)))
CJSON_CFLAGS := $(shell pkg-config --cflags libcjson)

UNAME_S := $(shell uname -s)

# Release: link third-party libraries statically where possible. Windows
# system DLLs (kernel32, user32, ...) remain dynamic; MinGW's -static does
# exactly that. On Unix we keep the platform linker defaults, since static
# libc is intentionally unsupported on macOS and discouraged on Linux.
ifeq ($(OS),Windows_NT)
REL_LIBS := -mwindows -static -static-libgcc -static-libstdc++ \
            $(filter-out -mwindows,$(shell pkg-config --static --libs $(PKGS)))
REL_LDFLAGS := -flto -Wl,--gc-sections -s
else ifeq ($(UNAME_S),Darwin)
REL_LIBS := $(PKG_LIBS)
REL_LDFLAGS := -flto -Wl,-dead_strip -Wl,-x
else
REL_LIBS := $(PKG_LIBS)
REL_LDFLAGS := -flto -Wl,--gc-sections -s
endif

WARN     := -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
CXXFLAGS ?= -std=c++20 -O2 -g $(WARN) -MMD -MP
CFLAGS   ?= -std=c99 -O2 -g

INCLUDES := -Isrc -Ibuild -Ithird_party/SDLGui -Ithird_party/libllmapi \
            -Ithird_party/base64/include

PROMPT_DIR   := assets/prompts
PROMPT_FILES := $(PROMPT_DIR)/sys.md $(PROMPT_DIR)/tool.md $(PROMPT_DIR)/title.md
PROMPT_INC   := build/default_prompts.inc

ICON_DIR := assets/icon
ICON_PNG := $(ICON_DIR)/icon.png
ICON_INC := build/icon_data.inc

CXXFLAGS += $(INCLUDES) $(PKG_CFLAGS)
CFLAGS   += -Ithird_party/libllmapi $(CJSON_CFLAGS)
LDLIBS   := $(PKG_LIBS)

BASE64_DIR := third_party/base64
BASE64_CFG := build/base64/config.h
BASE64_SRC := \
  $(BASE64_DIR)/lib/lib.c \
  $(BASE64_DIR)/lib/codec_choose.c \
  $(BASE64_DIR)/lib/tables/tables.c \
  $(BASE64_DIR)/lib/arch/generic/codec.c \
  $(BASE64_DIR)/lib/arch/avx/codec.c \
  $(BASE64_DIR)/lib/arch/avx2/codec.c \
  $(BASE64_DIR)/lib/arch/avx512/codec.c \
  $(BASE64_DIR)/lib/arch/neon32/codec.c \
  $(BASE64_DIR)/lib/arch/neon64/codec.c \
  $(BASE64_DIR)/lib/arch/sse41/codec.c \
  $(BASE64_DIR)/lib/arch/sse42/codec.c \
  $(BASE64_DIR)/lib/arch/ssse3/codec.c
BASE64_OBJ := $(patsubst $(BASE64_DIR)/%.c,build/base64/%.o,$(BASE64_SRC))

SRC      := $(wildcard src/*.cc)
OBJ      := $(patsubst src/%.cc,build/%.o,$(SRC))
OBJ      += build/llmapi.o
OBJ      += $(BASE64_OBJ)

ifeq ($(OS),Windows_NT)
RES_OBJ  := build/app_res.o
OBJ      += $(RES_OBJ)
endif

DEPS     := $(OBJ:.o=.d)

.PHONY: all debug release run clean

all: $(TARGET)

debug: CXXFLAGS := -std=c++20 -O0 -g3 $(WARN) -MMD -MP $(INCLUDES) $(PKG_CFLAGS)
debug: CFLAGS := -std=c99 -O0 -g3 -Ithird_party/libllmapi $(CJSON_CFLAGS)
debug: $(TARGET)

# Clean build with LTO, section GC, stripped symbols and static third-party
# libraries. This intentionally shares the normal output name.
release:
	$(MAKE) clean
	$(MAKE) \
	  CXXFLAGS="-std=c++20 -O3 -DNDEBUG -DCURL_STATICLIB $(WARN) -MMD -MP $(INCLUDES) $(PKG_CFLAGS) -flto -ffunction-sections -fdata-sections" \
	  CFLAGS="-std=c99 -O3 -DNDEBUG -Ithird_party/libllmapi $(CJSON_CFLAGS) -flto -ffunction-sections -fdata-sections" \
	  LDLIBS="$(REL_LIBS) $(REL_LDFLAGS)" \
	  all

$(TARGET): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(LDLIBS)

build/%.o: src/%.cc
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/prompt.o: $(PROMPT_INC)

build/app.o: $(ICON_INC)

$(EMBED_TOOL): tools/embed_prompts.cc
	@mkdir -p build
	$(CXX) -std=c++20 -O2 $< -o $@

$(EMBED_BIN_TOOL): tools/embed_binary.cc
	@mkdir -p build
	$(CXX) -std=c++20 -O2 $< -o $@

$(ICON_INC): $(EMBED_BIN_TOOL) $(ICON_PNG)
	$(EMBED_BIN_TOOL) $@ kPng=$(ICON_PNG)

ifeq ($(OS),Windows_NT)
$(RES_OBJ): $(ICON_DIR)/app.rc $(ICON_DIR)/icon.ico
	@mkdir -p build
	windres --include-dir $(ICON_DIR) $(ICON_DIR)/app.rc -O coff -o $@
endif

$(PROMPT_INC): $(EMBED_TOOL) $(PROMPT_FILES)
	$(EMBED_TOOL) $@ \
		kSys=$(PROMPT_DIR)/sys.md \
		kTool=$(PROMPT_DIR)/tool.md \
		kTitle=$(PROMPT_DIR)/title.md

build/llmapi.o: third_party/libllmapi/llmapi.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

$(BASE64_CFG):
	@mkdir -p build/base64
	@printf '#define HAVE_AVX512 0\n#define HAVE_AVX2 0\n#define HAVE_NEON32 0\n#define HAVE_NEON64 0\n#define HAVE_SSSE3 0\n#define HAVE_SSE41 0\n#define HAVE_SSE42 0\n#define HAVE_AVX 0\n' > $@

build/base64/%.o: $(BASE64_DIR)/%.c $(BASE64_CFG)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DBASE64_STATIC_DEFINE -I$(BASE64_DIR)/lib -Ibuild/base64 -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf build $(TARGET)

-include $(DEPS)
