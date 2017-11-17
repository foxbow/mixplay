# CC=/usr/bin/gcc
VERSION=$(shell git describe --tags --abbrev=1 --dirty=-dev --always)

CCFLAGS=-DVERSION=\"${VERSION}\"
CCFLAGS+=-Wall -g -pedantic
LDFLAGS_GLADE=`pkg-config --libs gtk+-3.0 gmodule-2.0` `pkg-config --cflags --libs x11`
CCFLAGS_GLADE=$(CCFLAGS) `pkg-config --cflags gtk+-3.0 gmodule-2.0`
DEPFLAGS = -MT $@ -MMD -MP -MF $*.Td

POSTCOMPILE = @mv -f $*.Td $*.d && touch $@

OBJS=utils.o musicmgr.o database.o mpgutils.o # config.o 
DOBJS=mixplayd.o player.o
NCOBJS=ncbox.o mixplay.o
GLOBJS=gladeutils.o gcallbacks.o gmixplay.o player.o config.o # gconfig.o 
EXES=bin/mixplay bin/gmixplay # bin/mixplayd


all: $(EXES)

clean:
	rm -f *.o
	rm -f *.gch
	rm -f $(EXES)
	
distclean: clean	
	rm -f gmixplay_app.h
	rm -f *~
	rm -f *.d
	rm -f core

bin/mixplay: $(OBJS) $(NCOBJS) 
	$(CC) $^ -o $@ -lncurses -lmpg123

#bin/mixplayd: $(OBJS) $(DOBJS) 
#	$(CC) $^ -o $@ -lmpg123

gmixplay.o: gmixplay_app.h

bin/gmixplay: $(OBJS) $(GLOBJS)
	$(CC) $(CCFLAGS_GLADE) $^ -o $@ $(LDFLAGS_GLADE) -lmpg123

# rules for GTK/GLADE
g%.o: g%.c
g%.o: g%.c g%.d
	$(CC) $(DEPFLAGS) $(CCFLAGS_GLADE) -c $<
	$(POSTCOMPILE)

# default
%.o: %.c
%.o: %.c %.d
	$(CC) $(DEPFLAGS) $(CCFLAGS) -c $<
	$(POSTCOMPILE)
	
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

%.d: ;
.PRECIOUS: %.d

include $(wildcard $(patsubst %,%.d,$(basename $(SRCS))))
