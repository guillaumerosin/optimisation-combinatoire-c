# Alternative a CMake : `make` (ou `mingw32-make` sous Windows).
CC ?= gcc
CFLAGS ?= -O3
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Isrc
LDLIBS = -lm -pthread

ifeq ($(OS),Windows_NT)
  EXE = .exe
endif

CORE = src/instance.c src/schedule.c src/pso.c src/platform.c
HEADERS = $(wildcard src/*.h)

all: build/jsp_pso$(EXE) build/jsp_tests$(EXE)

build/jsp_pso$(EXE): src/main.c $(CORE) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -pthread -o $@ src/main.c $(CORE) $(LDLIBS)

build/jsp_tests$(EXE): tests/test_core.c $(CORE) $(HEADERS)
	@mkdir -p build
	$(CC) $(CFLAGS) -pthread -o $@ tests/test_core.c $(CORE) $(LDLIBS)

test: build/jsp_tests$(EXE)
	cd build && ./jsp_tests$(EXE)

clean:
	rm -rf build

.PHONY: all test clean
