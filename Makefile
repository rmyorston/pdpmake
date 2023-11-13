# Makefile for make!
.POSIX:

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

OBJS = check.o input.o macro.o main.o make.o modtime.o rules.o target.o utils.o

make: $(OBJS)
	$(CC) -o make $(OBJS)

$(OBJS): make.h

install: make
	mkdir -p $(DESTDIR)$(BINDIR)
	cp -f make $(DESTDIR)$(BINDIR)/pdpmake

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/pdpmake

test: make
	@cd testsuite && ./runtest

clean:
	rm -f $(OBJS) make make.exe
