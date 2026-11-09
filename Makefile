PREFIX ?= $(HOME)/.local
CFLAGS ?= -O2 -Wall -Wno-deprecated-declarations
PKGS = gtk4-layer-shell-0 gtk4

snip: snip.c
	$(CC) $(CFLAGS) -o $@ $< $$(pkg-config --cflags --libs $(PKGS)) -lm

install: snip
	install -Dm755 snip $(PREFIX)/bin/snip

clean:
	rm -f snip
.PHONY: install clean
