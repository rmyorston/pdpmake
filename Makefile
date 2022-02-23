# Makefile for make!

OBJS = check.o input.o macro.o main.o make.o modtime.o rules.o target.o utils.o

make: $(OBJS)
	$(CC) -o make $(OBJS)

$(OBJS): make.h

test: make
	@cd testsuite && ./runtest

clean:
	rm -f $(OBJS) make
