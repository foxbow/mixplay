VERSION:=$(shell git describe --tags --abbrev=1 --dirty=-dev --always)
CCFLAGS=-DVERSION=\"${VERSION}\"
HDRS=utils.h ncbox.h musicmgr.h database.h mpgutils.h
OBJS=utils.o ncbox.o musicmgr.o database.o mpgutils.o
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

install: all
	install -s -m 0755 /bin/mixplay /usr/bin/
	install -m 0755 mixplay-nautilus.desktop /usr/share/applications/
	install -m 0644 mixplay.svg /usr/share/pixmaps/

prepare:
	apt-get install ncurses-dev mpg123 libmpg123-dev
