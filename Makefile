CC=/usr/bin/gcc
VERSION:=$(shell git describe --tags --abbrev=1 --dirty=-dev --always)
CCFLAGS=-DVERSION=\"${VERSION}\"
HDRS=utils.h ncbox.h musicmgr.h database.h mpgutils.h gladeutils.h
OBJS=utils.o musicmgr.o database.o mpgutils.o
NCOBJS=$(OBJS) ncbox.o
GLOBJS=$(OBJS) gladeutils.o
LDFLAGS=`pkg-config --libs gtk+-3.0 gmodule-2.0`
CCFLAGS+=`pkg-config --cflags gtk+-3.0 gmodule-2.0`
CCFLAGS+=-Wall -g 
EXES=bin/mixplay bin/gmixplay

# Keep object files
.PRECIOUS: %.o

all: $(EXES)

clean:
	rm -f *.o
	rm -f $(EXES)

bin/mixplay: $(NCOBJS) mixplay.o
	$(CC) $(CCFLAGS) $^ -o $@ -lncurses -lmpg123

bin/gmixplay: $(GLOBJS) gmixplay.o
	$(CC) $(CCFLAGS) $^ -o $@ $(LDFLAGS) -lmpg123

%.o: %.c $(HDRS)
	$(CC) $(CCFLAGS) -c $<

install: all
	install -s -m 0755 /bin/mixplay /usr/bin/
	install -m 0755 mixplay-nautilus.desktop /usr/share/applications/
	install -m 0644 mixplay.svg /usr/share/pixmaps/

prepare:
	apt-get install ncurses-dev mpg123 libmpg123-dev
