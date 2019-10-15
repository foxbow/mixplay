CC=gcc
VERSION=$(shell git describe --tags --abbrev=1 --dirty=-dev --always)
MPCOMM_VER=19
SRCDIR=src
OBJDIR=build

CCFLAGS=-DMPCOMM_VER="$(MPCOMM_VER)"
CCFLAGS+=-DVERSION=\"$(VERSION)\"
CCFLAGS+=-std=gnu99 -Wall -Wextra -pedantic -Werror -I . -g
#CCFLAGS+=-std=gnu99 -Wall -Wextra -Werror -pedantic -I . -O2

OBJS=$(addprefix $(OBJDIR)/,mpserver.o utils.o musicmgr.o database.o \
  mpgutils.o player.o config.o mpcomm.o json.o msgbuf.o mpinit.o mphid.o)

CLOBJS=$(addprefix $(OBJDIR)/,utils.o msgbuf.o config.o json.o mpclient.o)

LIBS=-lmpg123 -lpthread
REFS=alsa

# build with 2.7" ePaper support ?
ifeq ("$(shell dpkg -l wiringpi 2> /dev/null > /dev/null; echo $$?)","0")
LIBS+=-lwiringPi
OBJS+=$(OBJDIR)/mpepa.o $(OBJDIR)/epasupp.o
CCFLAGS+=-DEPAPER
else
$(info WiringPi is not installed, disabling ePaper support )
endif

# Install globally when called as root
ifeq ("$(shell id -un)","root")
BINDIR=/usr/local/bin
SHAREDIR=/usr/local/share
else
BINDIR=$(HOME)/bin
SHAREDIR=$(HOME)/.local/share
endif

ifneq ("$(shell cat $(OBJDIR)/CURVER)","$(MPCOMM_VER)")
$(shell touch static/mixplay_tmpl.js)
$(shell touch src/mpcomm.c)
$(shell echo ${MPCOMM_VER} > $(OBJDIR)/CURVER)
endif

LIBS+=$(shell pkg-config --libs $(REFS))
CCFLAGS+=$(shell pkg-config --cflags $(REFS))

all: $(OBJDIR)/dep.d bin/mixplayd bin/mprcinit

clean:
	rm -f $(OBJDIR)/*
	rm -f bin/mixplayd
	rm -f bin/mixplay-hid
	rm -f bin/mprcinit
	touch $(OBJDIR)/KEEPDIR

distclean: clean
	rm -f static/mixplay.js
	rm -f static/*~
	rm -f $(SRCDIR)/gmixplay_app.h
	rm -f $(SRCDIR)/mprc_html.h
	rm -f $(SRCDIR)/mixplayd_*.h
	rm -f $(SRCDIR)/mpplayer_*.h
	rm -f $(SRCDIR)/*~
	rm -f mixplayd
	rm -f mixplay-hid
	rm -f mprcinit
	rm -f core

new: clean all

client: bin/mixplay-hid

bin/mixplayd: $(OBJDIR)/mixplayd.o $(OBJS)
	$(CC) $^ -o $@ $(LIBS)

bin/mixplay-hid: $(OBJDIR)/mixplay-hid.o $(CLOBJS)
	$(CC) $^ -o $@ $(LIBS)

bin/mprcinit: $(OBJDIR)/mprcinit.o $(OBJS)
	$(CC) $^ -o $@ $(LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CCFLAGS) -c -o $@ $<

install: mixplayd
	install -d $(BINDIR)
	install -s -m 0755 mixplayd $(BINDIR)

$(OBJDIR)/mpplayer_html.h: static/mpplayer.html
	xxd -i static/mpplayer.html > $(OBJDIR)/mpplayer_html.h

$(OBJDIR)/mpplayer_js.h: static/mpplayer.js
	xxd -i static/mpplayer.js > $(OBJDIR)/mpplayer_js.h

$(OBJDIR)/mixplayd_html.h: static/mixplay.html
	xxd -i static/mixplay.html > $(OBJDIR)/mixplayd_html.h

$(OBJDIR)/mprc_html.h: static/mprc.html
	xxd -i static/mprc.html > $(OBJDIR)/mprc_html.h

$(OBJDIR)/mixplayd_css.h: static/mixplay.css
	xxd -i static/mixplay.css > $(OBJDIR)/mixplayd_css.h

$(OBJDIR)/mixplayd_js.h: static/mixplay.js
	xxd -i static/mixplay.js > $(OBJDIR)/mixplayd_js.h

$(OBJDIR)/mixplayd_svg.h: static/mixplay.js
	xxd -i static/mixplay.svg > $(OBJDIR)/mixplayd_svg.h

static/mixplay.js: static/mixplay_tmpl.js
	sed -e 's/~~MPCOMM_VER~~/'${MPCOMM_VER}'/g' -e 's/~~MIXPLAY_VER~~/'${VERSION}'/g' static/mixplay_tmpl.js > static/mixplay.js

prepare:
	apt-get install mpg123 libmpg123-dev libasound-dev

$(OBJDIR)/dep.d: src/*
	rm -f $(OBJDIR)/dep.d
	$(CC) -I . $(SRCDIR)/*.c -MM -MG | sed 's|[a-zA-Z0-9_-]*\.o|$(OBJDIR)/&|' >> $(OBJDIR)/dep.d

# This will fail silently of first make run
-include $(OBJDIR)/dep.d
