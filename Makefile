VERSION:=$(shell git describe --tags --long --dirty --always)
CCFLAGS=-DVERSION=\"${VERSION}\"
HDRS=utils.h ncutils.h musicmgr.h dbutils.h
OBJS=utils.o ncutils.o musicmgr.o dbutils.c
EXES=bin/mixplay bin/scanformusic
CCFLAGS+=-Wall # -g

# Keep object files
.PRECIOUS: %.o

all: $(EXES)

clean:
	rm -f *.o
	rm -f $(EXES)

bin/%: $(OBJS) %.o
	gcc $(CCFLAGS) $^ -o $@ -lncurses

%.o: %.c $(HDRS)
	gcc $(CCFLAGS) -c $<
