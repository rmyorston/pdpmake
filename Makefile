# Makefile for make!
.SUFFIXES:	.obj

.c.obj:
	c -c $(CFLAGS) $<


OBJS	=	check.obj input.obj macro.obj main.obj \
		make.obj reader.obj rules.obj

m:		$(OBJS)
	c -a 4000h -o m $(OBJS)

$(OBJS):	h.h
