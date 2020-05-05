# Mixplay binaries go here

## built binaries
these will not survive a 'make clean'

### mixplayd
the mixplay server

### mprcinit
utility to create a valid configuration and learn about a remote control. This shall move into the web interface.

### mixplay-hid
example standalone client that connects to a mixplay server, shows the current title and allows basic commands like the 'mixplay -d' HID interface.

### mixplay-scr
screen saver client for embedded set-ups that turns off the screen via DPMS after 10 minutes of idle time.

## Mixplay helper scripts
some bash scripts that depend on third party utilities but may come in handy for certain use cases. These are static and will not be touched by any make target build.

### gainer.sh
depends on 'mp3gain' to add Radio/Mix gain tags to all mp3 files in the tree down from the current directory.

_CAVEAT_: this *will* touch all files and *may* even destroy the collection!

### mpflirc.sh
depends on 'flirc_util' to let a FLIRC adapter learn keys from a new remote control. This will just get FLIRC to recognize the keypresses but does nt mean the mixplay knows about this. 'mprcinit' is still needed to connect mixplay and FLIRC. This is not needed if the FLIRC adapter already 'knows' the remote.

### valgrind.sh
depends on 'valgrind' to run a memory check on mixplay running in simple debug mode.
