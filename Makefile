CC=gcc
VERSION=$(shell git describe --tags --abbrev=1 --dirty=-dev --always)
SRCDIR=src
OBJDIR=build

CCFLAGS+=-DVERSION=\"$(VERSION)\"
ifdef MPDEBUG
CCFLAGS+=-std=gnu11 -Wall -Wextra -pedantic -Werror -I . -g
else
CCFLAGS+=-std=gnu11 -Wall -Wextra -Werror -pedantic -I . -O2
endif

OBJS=$(addprefix $(OBJDIR)/,mpserver.o utils.o musicmgr.o database.o \
  config.o mpcomm.o json.o msgbuf.o mpinit.o mphid.o mpgutils.o player.o \
	mpflirc.o mpalsa.o)

CLOBJS=$(addprefix $(OBJDIR)/,utils.o msgbuf.o config.o json.o mpclient.o \
  mpcomm.o )

HCOBJS=$(CLOBJS) $(addprefix $(OBJDIR)/,mphid.o)

SCLIBS=-lX11 -lXext -lpthread

LIBS=-lmpg123 -lpthread -lm
REFS=alsa

# Install globally when called as root
ifeq ("$(shell id -un)","root")
BINDIR=/usr/local/bin
else
BINDIR=$(HOME)/bin
endif

LIBS+=$(shell pkg-config --libs $(REFS))
CCFLAGS+=$(shell pkg-config --cflags $(REFS))

all: server clients

server: $(OBJDIR)/dep.d bin/mixplayd bin/mprcinit

clients: $(OBJDIR)/dep.d bin/mixplay-hid bin/mixplay-scr

clean:
	rm -f $(OBJDIR)/[!R]*
	rm -f bin/mixplayd
	rm -f bin/mixplay-*
	rm -f bin/mprcinit
	rm -f bin/minify
	rm -f static/mixplay.html
	rm -f static/mixplay.css
	rm -f static/mixplay.js

distclean: clean
	find . -name "*~" -exec rm \{\} \;
	rm -f $(SRCDIR)/mprc_html.h
	rm -f $(SRCDIR)/mixplay_*.h
	rm -f $(SRCDIR)/mpplayer_*.h
	rm -rf cov-int
	rm -f *cov.tgz
	rm -f core

new: clean all

clients: bin/mixplay-hid bin/mixplay-scr

bin/minify: $(SRCDIR)/minify.c
	$(CC) $^ -o $@

bin/mixplayd: $(OBJDIR)/mixplayd.o $(OBJS)
	$(CC) $^ -o $@ $(LIBS)

bin/mixplay-hid: $(OBJDIR)/mixplay-hid.o $(HCOBJS)
	$(CC) $^ -o $@ $(LIBS)

bin/mixplay-scr: $(OBJDIR)/mixplay-scr.o $(CLOBJS)
	$(CC) $^ -o $@ $(SCLIBS)

bin/mprcinit: $(OBJDIR)/mprcinit.o $(OBJS)
	$(CC) $^ -o $@ $(LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CCFLAGS) -c -o $@ $<

install: all
	install -d $(BINDIR)
	install -s -m 0755 bin/mixplayd $(BINDIR)
	install -s -m 0755 bin/mpstop $(BINDIR)
	install -s -m 0755 bin/mprcinit $(BINDIR)
	install -s -m 0755 bin/mixplay-hid $(BINDIR)
	install -s -m 0755 bin/mixplay-scr $(BINDIR)
	install -s -m 0755 bin/mpflirc.sh $(BINDIR)
	install -s -m 0755 bin/mpstream.sh $(BINDIR)
	install -s -m 0755 bin/mpgainer.sh $(BINDIR)
	install -s -m 0755 bin/mixplay-hid $(BINDIR)

# minified files
static/mixplay.html: bin/minify src/mixplay.html
	cat src/mixplay.html | bin/minify > static/mixplay.html

static/mixplay.js: bin/minify src/mixplay.js
	cat src/mixplay.js | bin/minify > static/mixplay.js

static/mixplay.css: bin/minify src/mixplay.css
	cat src/mixplay.css | bin/minify > static/mixplay.css

# minor files
$(OBJDIR)/mpplayer_html.h: static/mpplayer.html
	xxd -i static/mpplayer.html > $(OBJDIR)/mpplayer_html.h

$(OBJDIR)/mpplayer_js.h: static/mpplayer.js
	xxd -i static/mpplayer.js > $(OBJDIR)/mpplayer_js.h

$(OBJDIR)/mprc_html.h: static/mprc.html
	xxd -i static/mprc.html > $(OBJDIR)/mprc_html.h

$(OBJDIR)/mixplay_svg.h: static/mixplay.svg
	xxd -i static/mixplay.svg > $(OBJDIR)/mixplay_svg.h

$(OBJDIR)/mixplay_png.h: static/mixplay.png
	xxd -i static/mixplay.png > $(OBJDIR)/mixplay_png.h

$(OBJDIR)/manifest_json.h: static/manifest.json
	xxd -i static/manifest.json > $(OBJDIR)/manifest_json.h

$(OBJDIR)/mixplay_html.h: static/mixplay.html
	xxd -i static/mixplay.html > $(OBJDIR)/mixplay_html.h

$(OBJDIR)/mixplay_css.h: static/mixplay.css
	xxd -i static/mixplay.css > $(OBJDIR)/mixplay_css.h

$(OBJDIR)/mixplay_js.h: static/mixplay.js
	xxd -i static/mixplay.js > $(OBJDIR)/mixplay_js.h


prepare:
	apt-get install mpg123 libmpg123-dev libasound-dev

$(OBJDIR)/dep.d: src/*
	rm -f $(OBJDIR)/dep.d
	$(CC) -I . $(SRCDIR)/*.c -MM -MG | sed 's|[a-zA-Z0-9_-]*\.o|$(OBJDIR)/&|' >> $(OBJDIR)/dep.d

# This will fail silently of first make run
-include $(OBJDIR)/dep.d
