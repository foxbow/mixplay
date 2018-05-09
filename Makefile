# CC=/usr/bin/gcc
VERSION=$(shell git describe --tags --abbrev=1 --dirty=-dev --always)
MPCOMM_VER=11
CCFLAGS=-DMPCOMM_VER="${MPCOMM_VER}"
CCFLAGS+=-DVERSION=\"${VERSION}\"
CCFLAGS+=-Wall -pedantic -Werror -g

OBJS=mpserver.o utils.o musicmgr.o database.o mpgutils.o player.o config.o player.o mpcomm.o json.o
NCOBJS=cmixplay.o ncbox.o
GLOBJS=gladeutils.o gcallbacks.o gmixplay.o  
LIBS=-lmpg123 -lpthread
REFS=alsa
# daemon is always being built 
EXES=bin/mixplayd
INST=install-mixplayd

# Install globally when called as root
ifeq ("$(shell id -un)","root")
INST+=install-service
BINDIR=/usr/local/bin
SHAREDIR=/usr/local/share
else
BINDIR=$(HOME)/bin
SHAREDIR=$(HOME)/.local/share
endif

# build GTK client?
ifeq ("$(shell pkg-config --exists  gtk+-3.0 gmodule-2.0 x11; echo $$?)","0")
EXES+=bin/gmixplay
INST+=install-gmixplay
REFS+=gtk+-3.0 gmodule-2.0 x11
else
$(info GTK is not installed, not building gmixplay )
endif

# build ncurses client?
ifeq ("$(shell pkg-config --exists  ncurses; echo $$?)","0")
EXES+=bin/cmixplay
INST+=install-cmixplay
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
	rm -f dep.d
	touch bin/KEEPDIR

distclean: clean
	rm -f static/CURVER
	rm -f static/mixplay.js
	rm -f static/*~
	rm -f gmixplay_app.h
	rm -f mprc_html.h
	rm -f mixplayd_*.h
	rm -f *~
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

install-mixplayd: bin/mixplayd
	install -d $(BINDIR)
	install -s -m 0755 bin/mixplayd $(BINDIR)

install-cmixplay: bin/cmixplay
	install -d ~/bin/
	install -s -m 0755 bin/cmixplay $(BINDIR)

install-gmixplay: bin/gmixplay
	install -d $(BINDIR)
	install -s -m 0755 bin/gmixplay $(BINDIR)
	install -d ~/.local/share/icons/
	install -m 0644 static/mixplay.svg $(SHAREDIR)/icons/
	install -d $(SHAREDIR)/applications/
	desktop-file-install --dir=$(SHAREDIR)/applications -m 0755 --set-icon=$(SHAREDIR)/icons/mixplay.svg --rebuild-mime-info-cache static/gmixplay.desktop

install-service: install-mixplayd
	$(info No service support yet!)

install: $(INST)

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
