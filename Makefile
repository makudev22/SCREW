PREFIX   ?= /usr/local
CC       ?= gcc
WINCC    ?= x86_64-w64-mingw32-gcc
CFLAGS   ?= -O2
CFLAGS   += -std=gnu11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -Iinclude -fno-strict-aliasing -fPIE -MMD -MP
LDFLAGS  += -pie
LDLIBS   += -lpthread -lm -lX11

SRC      := $(shell find src -name '*.c' | sort)
ASM      := $(shell find src -name '*.S' | sort)
OBJ      := $(SRC:src/%.c=build/obj/%.o) $(ASM:src/%.S=build/obj/%.o)
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

build/obj/%.o: src/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

WINRES   ?= x86_64-w64-mingw32-windres

build/tests/%.exe: tests/samples/%.c $(wildcard tests/samples/*.rc)
	@mkdir -p $(dir $@)
	@if [ -f tests/samples/$*.rc ]; then $(WINRES) -O coff tests/samples/$*.rc -o build/tests/$*.res.o; fi
	$(WINCC) -O2 -o $@ $< $$([ -f tests/samples/$*.rc ] && echo build/tests/$*.res.o) -lws2_32 -lversion -lgdi32 -lsetupapi

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
