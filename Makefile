VERSION:=$(shell git describe --tags --abbrev=1 --dirty=-dev --always)
CCFLAGS=-DVERSION=\"${VERSION}\"
HDRS=utils.h ncbox.h musicmgr.h database.h mpgutils.h
OBJS=utils.o ncbox.o musicmgr.o database.c mpgutils.c
EXES=bin/mixplay
CCFLAGS+=-Wall -g

# Keep object files
.PRECIOUS: %.o

all: $(EXES)

clean:
	rm -f *.o
	rm -f $(EXES)

bin/%: $(OBJS) %.o
	gcc $(CCFLAGS) $^ -o $@ -lncurses -lmpg123

%.o: %.c $(HDRS)
	gcc $(CCFLAGS) -c $<
