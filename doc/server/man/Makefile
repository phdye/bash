# Makefile for bash-server man pages
# Converts markdown (.md) to roff via pandoc, installs both formats.

SHELL = /bin/sh

PANDOC = pandoc
INSTALL = /usr/bin/install -c
INSTALL_DATA = $(INSTALL) -m 644

MD2MAN = ./md2man.sh
BUILDDIR = build

# Installation directories (overridable from parent Makefile)
DESTDIR =
prefix = /usr/local
datarootdir = $(prefix)/share
mandir = $(datarootdir)/man
docdir = $(datarootdir)/doc/bash-server/man

man1dir = $(mandir)/man1
man3dir = $(mandir)/man3
man5dir = $(mandir)/man5
man7dir = $(mandir)/man7

SECTIONS = 1 3 5 7

# Collect all markdown sources
MD1 = $(wildcard man1/*.md)
MD3 = $(wildcard man3/*.md)
MD5 = $(wildcard man5/*.md)
MD7 = $(wildcard man7/*.md)
MD_ALL = $(MD1) $(MD3) $(MD5) $(MD7)

# Derive roff targets: man1/foo.1.md -> build/man1/foo.1
ROFF1 = $(patsubst man1/%.md,$(BUILDDIR)/man1/%,$(MD1))
ROFF3 = $(patsubst man3/%.md,$(BUILDDIR)/man3/%,$(MD3))
ROFF5 = $(patsubst man5/%.md,$(BUILDDIR)/man5/%,$(MD5))
ROFF7 = $(patsubst man7/%.md,$(BUILDDIR)/man7/%,$(MD7))
ROFF_ALL = $(ROFF1) $(ROFF3) $(ROFF5) $(ROFF7)

.PHONY: all man install install-man install-md uninstall clean

all: man

man: $(ROFF_ALL)

# Pattern rules for each section
$(BUILDDIR)/man1/%: man1/%.md $(MD2MAN)
	$(MD2MAN) $< $@

$(BUILDDIR)/man3/%: man3/%.md $(MD2MAN)
	$(MD2MAN) $< $@

$(BUILDDIR)/man5/%: man5/%.md $(MD2MAN)
	$(MD2MAN) $< $@

$(BUILDDIR)/man7/%: man7/%.md $(MD2MAN)
	$(MD2MAN) $< $@

install: install-man install-md

install-man: man
	$(INSTALL) -d "$(DESTDIR)$(man1dir)"
	$(INSTALL) -d "$(DESTDIR)$(man3dir)"
	$(INSTALL) -d "$(DESTDIR)$(man5dir)"
	$(INSTALL) -d "$(DESTDIR)$(man7dir)"
	@for f in $(BUILDDIR)/man1/*; do [ -f "$$f" ] && $(INSTALL_DATA) "$$f" "$(DESTDIR)$(man1dir)/" || true; done
	@for f in $(BUILDDIR)/man3/*; do [ -f "$$f" ] && $(INSTALL_DATA) "$$f" "$(DESTDIR)$(man3dir)/" || true; done
	@for f in $(BUILDDIR)/man5/*; do [ -f "$$f" ] && $(INSTALL_DATA) "$$f" "$(DESTDIR)$(man5dir)/" || true; done
	@for f in $(BUILDDIR)/man7/*; do [ -f "$$f" ] && $(INSTALL_DATA) "$$f" "$(DESTDIR)$(man7dir)/" || true; done

install-md:
	@for s in $(SECTIONS); do \
	    dest="$(DESTDIR)$(docdir)/man$$s"; \
	    if [ -d "man$$s" ]; then \
	        $(INSTALL) -d "$$dest"; \
	        for f in man$$s/*.md; do \
	            [ -f "$$f" ] && $(INSTALL_DATA) "$$f" "$$dest/" || true; \
	        done; \
	    fi; \
	done

uninstall:
	@for f in man1/*.md; do base=$$(basename "$$f" .md); rm -f "$(DESTDIR)$(man1dir)/$$base"; done
	@for f in man3/*.md; do base=$$(basename "$$f" .md); rm -f "$(DESTDIR)$(man3dir)/$$base"; done
	@for f in man5/*.md; do base=$$(basename "$$f" .md); rm -f "$(DESTDIR)$(man5dir)/$$base"; done
	@for f in man7/*.md; do base=$$(basename "$$f" .md); rm -f "$(DESTDIR)$(man7dir)/$$base"; done
	rm -rf "$(DESTDIR)$(docdir)"

clean:
	rm -rf $(BUILDDIR)
