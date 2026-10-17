PREFIX   ?= /usr/local
CC       ?= gcc
WINCC    ?= x86_64-w64-mingw32-gcc
CFLAGS   ?= -O2
CFLAGS   += -std=gnu11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -Iinclude -fno-strict-aliasing -fPIE -MMD -MP
LDFLAGS  += -pie
LDLIBS   += -lpthread -lm

SRC      := $(shell find src -name '*.c' | sort)
OBJ      := $(SRC:src/%.c=build/obj/%.o)
BIN      := build/screw

SAMPLES  := $(wildcard tests/samples/*.c)
EXES     := $(SAMPLES:tests/samples/%.c=build/tests/%.exe)

.PHONY: all clean install uninstall test samples

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

build/tests/%.exe: tests/samples/%.c
	@mkdir -p $(dir $@)
	$(WINCC) -O2 -o $@ $<

samples: $(EXES)

test: $(BIN) $(EXES)
	@tests/run.sh $(BIN) build/tests tests/expected

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/screw
	install -Dm644 dist/screw.desktop $(DESTDIR)$(PREFIX)/share/applications/screw.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/screw $(DESTDIR)$(PREFIX)/share/applications/screw.desktop

clean:
	rm -rf build

-include $(OBJ:.o=.d)
