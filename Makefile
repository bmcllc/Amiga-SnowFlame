# Hot-ice — renderizador de referência (C99)
# Arquitetura Homotopia · Console SnowFlame

CC     ?= gcc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Iinclude
AR     ?= ar

LIB_SRCS := src/context.c src/hde.c src/raster.c src/texture.c \
            src/hiqtc.c src/hgl.c src/hipng.c src/mapu.c
LIB_OBJS := $(LIB_SRCS:src/%.c=build/%.o)

DEMOS := build/demo01_tbr_morph build/demo02_hiqtc \
         build/demo03_mipmap build/demo04_reflection \
         build/demo05_shading build/demo06_hiqtc_p8 \
              build/demo07_mapu

.PHONY: all lib test demos run-demos clean

all: lib $(DEMOS) build/test_all

lib: build/libhotice.a

build:
	mkdir -p build

build/%.o: src/%.c src/internal.h include/hotice/types.h include/hotice/hgl.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/libhotice.a: $(LIB_OBJS)
	$(AR) rcs $@ $^

build/test_all: tests/test_main.c tests/test_mapu.c build/libhotice.a | build
	$(CC) $(CFLAGS) tests/test_main.c tests/test_mapu.c build/libhotice.a -lm -o $@

build/demo%: demos/demo%.c build/libhotice.a | build
	$(CC) $(CFLAGS) $< build/libhotice.a -lm -o $@

test: build/test_all
	./build/test_all

demos: $(DEMOS)

run-demos: demos
	cd build && ./demo01_tbr_morph && ./demo02_hiqtc && ./demo03_mipmap && ./demo04_reflection && ./demo05_shading && ./demo06_hiqtc_p8

clean:
	rm -rf build
