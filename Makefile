CC = gcc
WARN = -Wall -Wextra -Werror
CFLAGS = -O2 -g $(WARN)
INCLUDE =
LDFLAGS = -lresolv
DESTDIR ?=
PREFIX = /usr/local
VERSION ?= $(shell cat VERSION 2>/dev/null || echo "unknown")

BINDIR ?= $(DESTDIR)$(PREFIX)/bin
MANDIR ?= $(DESTDIR)$(PREFIX)/share/man/man1

BIN = wrapsrv
MAN = wrapsrv.1
SRC = wrapsrv.c
TARNAME = wrapsrv-$(VERSION)

all: $(BIN) $(DOC)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -DWRAPSRV_VERSION=\"$(VERSION)\" -o $@ $(SRC) $(INCLUDE) $(LDFLAGS)

$(MAN): wrapsrv.docbook
	docbook2x-man $<

clean:
	rm -f $(BIN) $(BIN)-asan
	$(MAKE) -C test clean 2>/dev/null || true

install: $(BIN)
	mkdir -p $(BINDIR)
	mkdir -p $(MANDIR)
	install -m 0755 $(BIN) $(BINDIR)
	install -m 0644 $(MAN) $(MANDIR)

test: $(BIN)
	$(MAKE) -C test

check: test
	cd test && ./run_tests.sh

asan:
	$(CC) $(CFLAGS) -DWRAPSRV_VERSION=\"$(VERSION)\" \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		-o $(BIN)-asan $(SRC) $(INCLUDE) $(LDFLAGS)

dist:
	mkdir -p $(TARNAME)
	cp $(SRC) list.h Makefile README.md LICENSE COPYRIGHT wrapsrv.1 $(TARNAME)/
	printf '%s\n' "$(VERSION)" > $(TARNAME)/VERSION
	tar czf $(TARNAME).tar.gz $(TARNAME)
	rm -rf $(TARNAME)

.PHONY: all clean install test check asan dist
