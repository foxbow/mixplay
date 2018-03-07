# CC=/usr/bin/gcc
VERSION=$(shell git describe --tags --abbrev=1 --dirty=-dev --always)
MPCOMM_VER=10
CCFLAGS=-DMPCOMM_VER="${MPCOMM_VER}"
CCFLAGS+=-DVERSION=\"${VERSION}\"
CCFLAGS+=-Wall -g -pedantic -Werror

OBJS=utils.o musicmgr.o database.o mpgutils.o player.o config.o player.o mpcomm.o json.o
NCOBJS=cmixplay.o ncbox.o
GLOBJS=gladeutils.o gcallbacks.o gmixplay.o  
LIBS=-lmpg123 -lpthread
REFS=alsa
# daemon is always being built 
EXES=bin/mixplayd 

# build GTK client?
ifeq ("$(shell pkg-config --exists  gtk+-3.0 gmodule-2.0 x11; echo $$?)","0")
EXES+=bin/gmixplay
REFS+=gtk+-3.0 gmodule-2.0 x11
else
$(info GTK is not installed, not building gmixplay )
endif

# build ncurses client?
ifeq ("$(shell pkg-config --exists  ncurses; echo $$?)","0")
EXES+=bin/cmixplay
REFS+=ncurses
else
$(info ncurses is not installed, not building cmixplay )
endif

ifneq ("$(shell cat static/CURVER)","${MPCOMM_VER}")
$(shell rm static/mixplay.js)
$(shell echo ${MPCOMM_VER} > static/CURVER)
endif

LIBS+=$(shell pkg-config --libs $(REFS))
CCFLAGS+=$(shell pkg-config --cflags $(REFS))

all: dep.d $(EXES)
	
clean:
	rm -f *.o
	rm -f *.gch
	rm -f bin/*
	touch bin/KEEPDIR
	
distclean: clean
	rm static/CURVER	
	rm static/mixplay.js
	rm -f gmixplay_app.h
	rm -f mixplayd_*.h
	rm -f *~
	rm -f *.d
	rm -f *.Td
	rm -f core

new: clean all

bin/mixplayd: mixplayd.o $(OBJS) 
	$(CC) $^ -o $@ $(LIBS)

bin/cmixplay: cmixplay.o $(OBJS) $(NCOBJS) 
	$(CC) $^ -o $@ $(LIBS)

bin/gmixplay: $(OBJS) $(GLOBJS)
	$(CC) $^ -o $@ $(LIBS)

%.o: %.c
	$(CC) $(CCFLAGS) -c $<
	
install: all	
	install -d ~/bin/
	install -s -m 0755 bin/cmixplay ~/bin/
	install -s -m 0755 bin/gmixplay ~/bin/
	install -s -m 0755 bin/mixplayd ~/bin/
	install -d ~/.local/share/icons/
	install -m 0644 static/mixplay.svg ~/.local/share/icons/
	install -d ~/.local/share/applications/
	desktop-file-install --dir=$(HOME)/.local/share/applications -m 0755 --set-key=Exec --set-value="$(HOME)/bin/gmixplay %u" --set-icon=$(HOME)/.local/share/icons/mixplay.svg --rebuild-mime-info-cache static/gmixplay.desktop

# disabled until static pages are internal part of mixplayd	
install-global: all
	install -s -m 0755 bin/cmixplay /usr/bin/
	install -s -m 0755 bin/gmixplay /usr/bin/
	install -s -m 0755 bin/mixplayd /usr/bin/
	install -m 0644 static/mixplay.svg /usr/share/pixmaps/
	desktop-file-install -m 0755 --rebuild-mime-info-cache static/gmixplay.desktop 

mixplayd_html.h: static/mixplay.html
	xxd -i static/mixplay.html > mixplayd_html.h

mprc_html.h: static/mprc.html
	xxd -i static/mprc.html > mprc_html.h

mixplayd_css.h: static/mixplay.css
	xxd -i static/mixplay.css > mixplayd_css.h

static/mixplay.js: static/mixplay_js.tmpl
	sed -e 's/~~MPCOMM_VER~~/'${MPCOMM_VER}'/g'  static/mixplay_js.tmpl > static/mixplay.js
	
mixplayd_js.h: static/mixplay.js
	xxd -i static/mixplay.js > mixplayd_js.h

gmixplay_app.h: static/gmixplay_app.glade
	xxd -i static/gmixplay_app.glade > gmixplay_app.h
	
prepare:
	apt-get install ncurses-dev mpg123 libmpg123-dev libgtk-3-dev libasound-dev

dep.d: *.h
	rm -f dep.d
	gcc *.c -MM -MG >> dep.d
	
# This will fail silently of first make run
-include dep.d
