NAME := agbemu_libretro
# CC := gcc

CFLAGS := -Wall -Wimplicit-fallthrough -Wno-format -Werror -fPIC
#CFLAGS_RELEASE := -O3 -flto
CFLAGS_RELEASE := -O2
CFLAGS_DEBUG := -g -O0

#CPPFLAGS := -MP -MMD
LDFLAGS := -lm

ifeq ($(shell uname),Darwin)
	CPPFLAGS += -I$(shell brew --prefix)/include
	LDFLAGS := -L$(shell brew --prefix)/lib $(LDFLAGS)
endif

GIT_VERSION := $(shell git describe --abbrev=7 --always)
ARGS := -D__LIBRETRO__ -DVERSION=\"$(GIT_VERSION)\"

ifeq ($(OS),Windows_NT)
  ARGS += -static -DWINDOWS
  SHARED_EXT := .dll
else
  ifeq ($(shell uname -s),Darwin)
    ARGS += -DMACOS
    SHARED_EXT := .dylib
  else
    SHARED_EXT := .so
  endif
endif

TARGET_LIB = $(NAME)$(SHARED_EXT)

BUILD_DIR := build-libretro
SRC_DIR := ../src
LIBRETRO_DIR := ../libretro

SHARED := -shared -Wl,-version-script=link.T -Wl,-no-undefined

LDFLAGS += $(SHARED)

DEBUG_DIR := $(BUILD_DIR)/debug
RELEASE_DIR := $(BUILD_DIR)/release

SRCS := \
	$(SRC_DIR)/apu.c \
	$(SRC_DIR)/arm_isa.c \
	$(SRC_DIR)/arm7tdmi.c \
	$(SRC_DIR)/cartridge.c \
	$(SRC_DIR)/debugger.c \
	$(SRC_DIR)/dma.c \
	$(SRC_DIR)/gba.c \
	$(SRC_DIR)/gpio.c \
	$(SRC_DIR)/io.c \
	$(SRC_DIR)/ppu.c \
	$(SRC_DIR)/scheduler.c \
	$(SRC_DIR)/thumb_isa.c \
	$(SRC_DIR)/timer.c

INCFLAG := -I$(SRC_DIR) -I../libretro

OBJS_DEBUG := $(SRCS:%.c=$(DEBUG_DIR)/%.o)
DEPS_DEBUG := $(OBJS_DEBUG:.o=.d)

OBJS_RELEASE := $(SRCS:%.c=$(RELEASE_DIR)/%.o)
DEPS_RELEASE := $(OBJS_RELEASE:.o=.d)

OBJS_DEBUG += $(DEBUG_DIR)/libretro.o
OBJS_RELEASE += $(RELEASE_DIR)/libretro.o

.PHONY: release, debug, clean

release: CFLAGS += $(CFLAGS_RELEASE)
release: $(RELEASE_DIR)/$(TARGET_LIB)

debug: CFLAGS += $(CFLAGS_DEBUG)
debug: $(DEBUG_DIR)/$(TARGET_LIB)

$(RELEASE_DIR)/$(TARGET_LIB): $(OBJS_RELEASE)
	$(CC) -o $@ $(CFLAGS) $(CPPFLAGS) $(ARGS) $^ $(LDFLAGS)
	cp $@ $(TARGET_LIB)

$(RELEASE_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ARGS) -c $< -o $@ $(INCFLAG)

$(DEBUG_DIR)/$(TARGET_LIB): $(OBJS_DEBUG)
	$(CC) -o $@ $(CFLAGS) $(CPPFLAGS) $(ARGS) $^ $(LDFLAGS)
	cp $@ $(TARGET_LIB)

$(DEBUG_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ARGS) -c $< -o $@ $(INCFLAG)

$(DEBUG_DIR)/%.o: $(LIBRETRO_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ARGS) -c $< -o $@ $(INCFLAG)

$(RELEASE_DIR)/%.o: $(LIBRETRO_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ARGS) -c $< -o $@ $(INCFLAG)

clean:
	rm -rf $(BUILD_DIR) $(TARGET_LIB)

-include $(DEPS_DEBUG)
-include $(DEPS_RELEASE)
