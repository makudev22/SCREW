PREFIX   ?= /usr/local
CC       ?= gcc
WINCC    ?= x86_64-w64-mingw32-gcc
CFLAGS   ?= -O2
CFLAGS   += -std=gnu11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -Iinclude -fno-strict-aliasing -fPIE -MMD -MP
LDFLAGS  += -pie
LDLIBS   += -lpthread -lm -lX11

SRC      := $(shell find src -name '*.c' | sort)
OBJ      := $(SRC:src/%.c=build/obj/%.o)
BIN      := build/screw

LIBOBJ   := $(filter-out build/obj/cli/main.o,$(OBJ))
UNITS    := $(patsubst tests/unit/%.c,build/unit/%,$(wildcard tests/unit/*.c))

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
	$(WINCC) -O2 -o $@ $< -lws2_32

build/unit/%: tests/unit/%.c $(LIBOBJ)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIBOBJ) $(LDLIBS)

samples: $(EXES)

test: $(BIN) $(EXES) $(UNITS)
	@for u in $(UNITS); do $$u || exit 1; done
	@tests/run.sh $(BIN) build/tests tests/expected

install: $(BIN)
	install -Dm755 $(BIN) $(DESTDIR)$(PREFIX)/bin/screw
	install -Dm644 dist/screw.desktop $(DESTDIR)$(PREFIX)/share/applications/screw.desktop

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/screw $(DESTDIR)$(PREFIX)/share/applications/screw.desktop

clean:
	rm -rf build

-include $(OBJ:.o=.d)
