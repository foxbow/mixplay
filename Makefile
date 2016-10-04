VERSION:=$(shell git describe --tags --long --dirty --always)
CCFLAGS=-DVERSION=\"${VERSION}\"
HDRS=utils.h ncutils.h musicmgr.h dbutils.h mpgutils.h
OBJS=utils.o ncutils.o musicmgr.o dbutils.c mpgutils.c
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
