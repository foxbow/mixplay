CC=/usr/bin/gcc
VERSION:=$(shell git describe --tags --abbrev=1 --dirty=-dev --always)
CCFLAGS=-DVERSION=\"${VERSION}\"
CCFLAGS+=-Wall -g
OBJS=utils.o musicmgr.o database.o mpgutils.o
NCOBJS=ncbox.o mixplay.o
GLOBJS=gladeutils.o callbacks.o gmixplay.o player.o
GLSRC=gmixplay.c gladeutils.c callbacks.c player.c
LDFLAGS_GLADE=`pkg-config --libs gtk+-3.0 gmodule-2.0` `pkg-config --cflags --libs x11`
CCFLAGS_GLADE=$(CCFLAGS) `pkg-config --cflags gtk+-3.0 gmodule-2.0`
EXES=bin/mixplay bin/gmixplay

# Keep object files
.PRECIOUS: %.o

all: $(EXES)

clean:
	rm -f *.o
	rm -f $(EXES)
	
distclean: clean	
	rm -f gmixplay_app.h
	rm -f gmixplay_fs.h
	rm -f *~

bin/mixplay: $(OBJS) $(NCOBJS) 
	$(CC) $(CCFLAGS_NCURSES) $^ -o $@ -lncurses -lmpg123

bin/gmixplay: $(OBJS) $(GLOBJS)
	$(CC) $(CCFLAGS_GLADE) $^ -o $@ $(LDFLAGS_GLADE) -lmpg123

%.o: %.c
	$(CC) $(CCFLAGS_GLADE) -c $<
	
install: all	
	install -d ~/bin/
	install -s -m 0755 bin/mixplay ~/bin/
	install -s -m 0755 bin/gmixplay ~/bin/
	install -d ~/.local/share/icons/
	install -m 0644 mixplay.svg ~/.local/share/icons/
	install -d ~/.local/share/applications/
	desktop-file-install --dir=$(HOME)/.local/share/applications -m 0755 --set-key=Exec --set-value="$(HOME)/bin/gmixplay %u" --set-icon=$(HOME)/.local/share/icons/mixplay.svg --rebuild-mime-info-cache gmixplay.desktop 
	
install-global: all
	install -s -m 0755 bin/mixplay /usr/bin/
	install -s -m 0755 bin/gmixplay /usr/bin/
	install -m 0644 mixplay.svg /usr/share/pixmaps/
	desktop-file-install -m 0755 --rebuild-mime-info-cache gmixplay.desktop 

gmixplay_app.h: gmixplay_app.glade
	xxd -i gmixplay_app.glade > gmixplay_app.h
	
prepare:
	apt-get install ncurses-dev mpg123 libmpg123-dev libgtk-3-dev

# Header Dependencies
callbacks.o: callbacks.c player.h gladeutils.h utils.h musicmgr.h
database.o: database.c database.h musicmgr.h utils.h mpgutils.h
gladeutils.o: gladeutils.c player.h gladeutils.h utils.h musicmgr.h
gmixplay.o: gmixplay.c utils.h musicmgr.h database.h gladeutils.h gmixplay_app.h
mixplay.o: mixplay.c utils.h musicmgr.h database.h mpgutils.h ncbox.h
mpgutils.o: mpgutils.c mpgutils.h musicmgr.h utils.h
musicmgr.o: musicmgr.c musicmgr.h mpgutils.h utils.h
ncbox.o: ncbox.c ncbox.h utils.h musicmgr.h
player.o: player.c player.h gladeutils.h utils.h musicmgr.h
utils.o: utils.c utils.h
