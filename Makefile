# Makefile for make!



OBJS	=	check.o input.o macro.o main.o \
		make.o reader.o rules.o

m:		$(OBJS)
	cc -o m $(OBJS)

$(OBJS):	h.h
