CC=gcc
VERSION=$(shell git describe --tags --abbrev=1 --dirty=-dev --always)
MPCOMM_VER=15
SRCDIR=src
OBJDIR=build

CCFLAGS=-DMPCOMM_VER="$(MPCOMM_VER)"
CCFLAGS+=-DVERSION=\"$(VERSION)\"
CCFLAGS+=-Wall -pedantic -Werror -I $(OBJDIR) -g

OBJS=$(addprefix $(OBJDIR)/,mpserver.o utils.o musicmgr.o database.o \
  mpgutils.o player.o config.o player.o mpcomm.o json.o )
NCOBJS=$(addprefix $(OBJDIR)/,cmixplay.o ncbox.o )
GLOBJS=$(addprefix $(OBJDIR)/,gladeutils.o gcallbacks.o gmixplay.o )

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

ifneq ("$(shell cat $(OBJDIR)/CURVER)","$(MPCOMM_VER)")
$(shell rm -f static/mixplay.js)
$(shell echo ${MPCOMM_VER} > $(OBJDIR)/CURVER)
endif

LIBS+=$(shell pkg-config --libs $(REFS))
CCFLAGS+=$(shell pkg-config --cflags $(REFS))

all: $(OBJDIR)/dep.d $(EXES)

clean:
	rm -f bin/*
	rm -f $(OBJDIR)/*
	touch bin/KEEPDIR
	touch $(OBJDIR)/KEEPDIR

distclean: clean
	rm -f static/mixplay.js
	rm -f static/*~
	rm -f $(SRCDIR)/gmixplay_app.h
	rm -f $(SRCDIR)/mprc_html.h
	rm -f $(SRCDIR)/mixplayd_*.h
	rm -f $(SRCDIR)/mpplayer_*.h
	rm -f $(SRCDIR)/*~
	rm -f core

new: clean all

bin/mixplayd: $(OBJDIR)/mixplayd.o $(OBJS)
	$(CC) $^ -o $@ $(LIBS)

bin/cmixplay: $(OBJDIR)/cmixplay.o $(OBJS) $(NCOBJS)
	$(CC) $^ -o $@ $(LIBS)

bin/gmixplay: $(OBJS) $(GLOBJS)
	$(CC) $^ -o $@ $(LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CCFLAGS) -c -o $@ $<

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
	install -m 0644 glade/mixplay.svg $(SHAREDIR)/icons/
	install -d $(SHAREDIR)/applications/
	desktop-file-install --dir=$(SHAREDIR)/applications -m 0755 --set-icon=$(SHAREDIR)/icons/mixplay.svg --rebuild-mime-info-cache glade/gmixplay.desktop

install-service: install-mixplayd
	$(info No service support yet!)

install: $(INST)

mpplayer_html.h: static/mpplayer.html
	xxd -i static/mpplayer.html > $(OBJDIR)/mpplayer_html.h

mpplayer_js.h: static/mpplayer.js
	xxd -i static/mpplayer.js > $(OBJDIR)/mpplayer_js.h

mixplayd_html.h: static/mixplay.html
	xxd -i static/mixplay.html > $(OBJDIR)/mixplayd_html.h

mprc_html.h: static/mprc.html
	xxd -i static/mprc.html > $(OBJDIR)/mprc_html.h

mixplayd_css.h: static/mixplay.css
	xxd -i static/mixplay.css > $(OBJDIR)/mixplayd_css.h

mixplayd_js.h: static/mixplay.js
	xxd -i static/mixplay.js > $(OBJDIR)/mixplayd_js.h

gmixplay_app.h: glade/gmixplay_app.glade
	xxd -i glade/gmixplay_app.glade > $(OBJDIR)/gmixplay_app.h

static/mixplay.js: static/mixplay_js.tmpl
	sed -e 's/~~MPCOMM_VER~~/'${MPCOMM_VER}'/g' -e 's/~~MP_VERSION~~/'${VERSION}'/g' static/mixplay_js.tmpl > static/mixplay.js

prepare:
	apt-get install ncurses-dev mpg123 libmpg123-dev libgtk-3-dev libasound-dev

$(OBJDIR)/dep.d: src/*.h
	rm -f $(OBJDIR)/dep.d
	$(CC) $(SRCDIR)/*.c -MM -MG | sed 's|[a-zA-Z0-9_-]*\.o|$(OBJDIR)/&|' >> $(OBJDIR)/dep.d

# This will fail silently of first make run
-include $(OBJDIR)/dep.d
