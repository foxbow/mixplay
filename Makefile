# CC=/usr/bin/gcc
VERSION=$(shell git describe --tags --abbrev=1 --dirty=-dev --always)

CCFLAGS=-DVERSION=\"${VERSION}\"
CCFLAGS+=-Wall -g -pedantic
LDFLAGS_GLADE=`pkg-config --libs gtk+-3.0 gmodule-2.0` `pkg-config --cflags --libs x11`
CCFLAGS_GLADE=$(CCFLAGS) `pkg-config --cflags gtk+-3.0 gmodule-2.0`

OBJS=utils.o musicmgr.o database.o mpgutils.o player.o config.o player.o mpcomm.o 
DOBJS=mixplayd.o 
NCOBJS=cmixplay.o ncbox.o
GLOBJS=gladeutils.o gcallbacks.o gmixplay.o  
LIBS=-lmpg123 -lasound -lpthread

EXES=bin/cmixplay bin/mixplayd bin/gmixplay

all: $(EXES)

clean:
	rm -f *.o
	rm -f *.gch
	rm -f $(EXES)
	
distclean: clean	
	rm -f gmixplay_app.h
	rm -f *~
	rm -f *.d
	rm -f *.Td
	rm -f core

bin/cmixplay: cmixplay.o player.o config.o $(OBJS) $(NCOBJS) 
	$(CC) $^ -o $@ -lncurses $(LIBS)

bin/mixplayd: $(OBJS) $(DOBJS) 
	$(CC) $^ -o $@ $(LIBS)

bin/gmixplay: $(OBJS) $(GLOBJS)
	$(CC) $^ -o $@ $(LDFLAGS_GLADE) $(LIBS)

# rules for GTK/GLADE
g%.o: g%.c
	$(CC) $(CCFLAGS_GLADE) -c $<

# default
%.o: %.c
	$(CC) $(CCFLAGS) -c $<
	
install: all	
	install -d ~/bin/
	install -s -m 0755 bin/cmixplay ~/bin/
	install -s -m 0755 bin/gmixplay ~/bin/
	install -s -m 0755 bin/mixplayd ~/bin/
	install -d ~/.local/share/icons/
	install -m 0644 mixplay.svg ~/.local/share/icons/
	install -d ~/.local/share/applications/
	desktop-file-install --dir=$(HOME)/.local/share/applications -m 0755 --set-key=Exec --set-value="$(HOME)/bin/gmixplay %u" --set-icon=$(HOME)/.local/share/icons/mixplay.svg --rebuild-mime-info-cache gmixplay.desktop 
	
install-global: all
	install -s -m 0755 bin/cmixplay /usr/bin/
	install -s -m 0755 bin/gmixplay /usr/bin/
	install -s -m 0755 bin/mixplayd /usr/bin/
	install -m 0644 mixplay.svg /usr/share/pixmaps/
	desktop-file-install -m 0755 --rebuild-mime-info-cache gmixplay.desktop 

gmixplay_app.h: gmixplay_app.glade
	xxd -i gmixplay_app.glade > gmixplay_app.h
	
prepare:
	apt-get install ncurses-dev mpg123 libmpg123-dev libgtk-3-dev libasound-dev

dep.d:
	rm -f dep.d
	gcc *.c -MM -MG >> dep.d
	
# This will fail silently of first make run
-include dep.d
