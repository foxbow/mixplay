CC=gcc
VERSION=$(shell git describe --tags --abbrev=1 --dirty=-dev --always | sed -e 's/-[0-9,a-z]*$$//' -e 's/-/\./' )
SRCDIR=src
OBJDIR=build

CCFLAGS+=-DVERSION=\"$(VERSION)\"
CCFLAGS+=-D_GNU_SOURCE -std=gnu11 -Wall -Wextra -Werror -pedantic -I .

ifdef MPDEBUG
# force compilation with -g
CCFLAGS+=-g
ifeq ($(MPDEBUG),2)
# enable address sanitizer
CCFLAGS+=-fsanitize=address
LIBS=-lasan
endif
else
# master branch is built with -O2, dev branches with -g
ifeq ($(shell git rev-parse --abbrev-ref HEAD),master)
CCFLAGS+=-O2
else
CCFLAGS+=-g
endif
endif

OBJS=$(addprefix $(OBJDIR)/,mpserver.o utils.o musicmgr.o database.o \
  config.o mpcomm.o json.o msgbuf.o mpinit.o mphid.o mpgutils.o player.o \
	mpflirc.o mpalsa.o controller.o)

CLOBJS=$(addprefix $(OBJDIR)/,utils.o msgbuf.o config.o json.o mpclient.o \
  mpcomm.o )

HCOBJS=$(CLOBJS) $(addprefix $(OBJDIR)/,mphid.o)

LIBS+=-lpthread -lm
REFS=alsa libmpg123

CLIENTS=bin/mixplay-hid
ifeq ($(shell pkg-config --exists xcomposite && echo 0),0)
CLIENTS+=bin/mixplay-scr
else
$(info No X11 support (libxcomposite-dev), screensaver client will not be built)
endif

# Install globally when called as root
ifeq ("$(shell id -un)","root")
BINDIR=/usr/local/bin
else
BINDIR=$(HOME)/bin
endif

LIBS+=$(shell pkg-config --libs $(REFS))
CCFLAGS+=$(shell pkg-config --cflags $(REFS))

all: sanity server clients

# rudimentary sanity check
.PHONY sanity:
ifeq ($(shell pkg-config --exists alsa && echo 1),)
	$(error ALSA support is not installed. Consider running 'make prepare' )
endif
ifeq ($(shell pkg-config --exists libmpg123 && echo 1),)
	$(error libmpg123 is not installed. Consider running 'make prepare' )
endif

server: $(OBJDIR)/dep.d bin/mixplayd bin/mprcinit

clients: $(OBJDIR)/dep.d $(CLIENTS)

# handy when vscode messes up the dependency file
nodep: remdep all

remdep:
	rm -f build/dep.d

clean:
	rm -f $(OBJDIR)/[!R]*
	rm -f bin/mixplayd
	rm -f bin/mixplay-*
	rm -f bin/mprcinit
	rm -f bin/minify
	rm -f bin/test
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

bin/minify: $(SRCDIR)/minify.c
	$(CC) $^ -o $@

bin/mixplayd: $(OBJDIR)/mixplayd.o $(OBJS)
	$(CC) $^ -o $@ $(LIBS)

bin/test: $(OBJDIR)/test.o $(OBJS)
	$(CC) $^ -o $@ $(LIBS)

bin/mixplay-hid: $(OBJDIR)/mixplay-hid.o $(HCOBJS)
	$(CC) $^ -o $@ $(LIBS)

bin/mixplay-scr: $(OBJDIR)/mixplay-scr.o $(CLOBJS)
	$(CC) $^ -o $@ -lpthread -lX11 -lXext $(LIBS)

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

$(OBJDIR)/%_html.h: $(SRCDIR)/%.html bin/minify
	cat $< | bin/minify > static/$(notdir $<)
	xxd -i static/$(notdir $<) > $@

$(OBJDIR)/%_js.h: $(SRCDIR)/%.js bin/minify
	cat $< | bin/minify > static/$(notdir $<)
	xxd -i static/$(notdir $<) > $@

$(OBJDIR)/%_css.h: $(SRCDIR)/%.css bin/minify
	cat $< | bin/minify > static/$(notdir $<)
	xxd -i static/$(notdir $<) > $@

$(OBJDIR)/mixplay_svg.h: static/mixplay.svg
	xxd -i static/mixplay.svg > $(OBJDIR)/mixplay_svg.h

$(OBJDIR)/mixplay_png.h: static/mixplay.png
	xxd -i static/mixplay.png > $(OBJDIR)/mixplay_png.h

$(OBJDIR)/manifest_json.h: static/manifest.json
	xxd -i static/manifest.json > $(OBJDIR)/manifest_json.h

prepare:
	sudo apt-get install mpg123 libmpg123-dev libasound-dev pkg-config

$(OBJDIR)/dep.d: src/*
	rm -f $(OBJDIR)/dep.d
	$(CC) -E $(SRCDIR)/*.c -MM -MG | sed 's|[a-zA-Z0-9_-]*\.o|$(OBJDIR)/&|' > $(OBJDIR)/dep.d

# This will fail silently of first make run
-include $(OBJDIR)/dep.d
