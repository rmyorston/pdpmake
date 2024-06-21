# Makefile for make!
.POSIX:
.PHONY: install uninstall test clean

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man

OBJS = check.o input.o macro.o main.o make.o modtime.o rules.o target.o utils.o

make: $(OBJS)
	$(CC) $(LDFLAGS) -o make $(OBJS)

$(OBJS): make.h

install: make
	test -d $(DESTDIR)$(BINDIR) || mkdir -p $(DESTDIR)$(BINDIR)
	cp -f make $(DESTDIR)$(BINDIR)/pdpmake
	test -d $(DESTDIR)$(MANDIR)/man1 || mkdir -p $(DESTDIR)$(MANDIR)/man1
	cp -f pdpmake.1 $(DESTDIR)$(MANDIR)/man1/pdpmake.1

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/pdpmake
	rm -f $(DESTDIR)$(MANDIR)/man1/pdpmake.1

test: make
	@cd testsuite && ./runtest

clean:
	rm -f $(OBJS) make
