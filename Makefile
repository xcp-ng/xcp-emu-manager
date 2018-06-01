BINDIR ?= /usr/bin
LIBEXECDIR ?= /usr/libexec

.PHONY: build install uninstall clean

build:
	jbuilder build @install

install:
	mkdir -p $(DESTDIR)$(BINDIR)
	cp _build/default/bin/emu_manager.exe $(DESTDIR)$(BINDIR)/emu-manager

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/emu-manager

clean:
	jbuilder clean
