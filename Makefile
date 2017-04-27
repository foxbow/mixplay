CC=/usr/bin/gcc
VERSION:=$(shell git describe --tags --abbrev=1 --dirty=-dev --always)
CCFLAGS=-DVERSION=\"${VERSION}\"
CCFLAGS+=-Wall -g -DDEBUG
HDRS=utils.h musicmgr.h database.h mpgutils.h 
OBJS=utils.o musicmgr.o database.o mpgutils.o
NCOBJS=ncbox.o mixplay.o
GLOBJS=gladeutils.o callbacks.o gmixplay.o player.o
GLSRC=gmixplay.c gladeutils.c callbacks.c player.c
LDFLAGS_GLADE=`pkg-config --libs gtk+-3.0 gmodule-2.0`
CCFLAGS_GLADE=$(CCFLAGS) `pkg-config --cflags gtk+-3.0 gmodule-2.0`
EXES=bin/mixplay bin/gmixplay

# Keep object files
.PRECIOUS: %.o

all: $(EXES)

clean:
	rm -f *.o
	rm -f $(EXES)

bin/mixplay: $(OBJS) $(NCOBJS) ncbox.h 
	$(CC) $(CCFLAGS_NCURSES) $^ -o $@ -lncurses -lmpg123

bin/gmixplay: $(OBJS) $(GLOBJS) gladeutils.h player.h
	$(CC) $(CCFLAGS_GLADE) $^ -o $@ $(LDFLAGS_GLADE) -lmpg123

%.o: %.c $(HDRS)
	$(CC) $(CCFLAGS_GLADE) -c $<

install: all
	install -s -m 0755 /bin/mixplay /usr/bin/
	install -s -m 0755 /bin/gmixplay /usr/bin/
	install -m 0755 mixplay-nautilus.desktop /usr/share/applications/
	install -m 0644 mixplay.svg /usr/share/pixmaps/

prepare:
	apt-get install ncurses-dev mpg123 libmpg123-dev libgtk-3-dev
