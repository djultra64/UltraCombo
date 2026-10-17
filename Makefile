CC      ?= gcc
CFLAGS  ?= -O2 -g -Wall -Wextra -Wno-unused-parameter
CFLAGS  += $(shell pkg-config --cflags sdl2)
LDLIBS  += $(shell pkg-config --libs sdl2) -lm

SRC = src/main.c src/cpu.c src/disasm.c src/mem.c src/ide.c src/dcs.c src/adsp.c
OBJ = $(SRC:src/%.c=build/%.o)

ki2: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c src/*.h | build
	$(CC) $(CFLAGS) -c -o $@ $<

build:
	mkdir -p build

clean:
	rm -rf build ki2

.PHONY: clean
