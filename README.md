# mixplayd
the new heartpiece of the mixplay family. This is the manager and player that can run headless and will just play the shuffled Music content. Different front-ends may connect to the mixplayd and control the actions.

This is destined to run on a headless box and supply the stereo with a constant stream of music.

### Features
* Music database to avoid scanning on every start
* Skipping titles forward/backward
* Jumping forward backward during play
* favourites
* do-not-play lists
* shuffle play that avoids the same artist twice in a row
* shuffle will play each title before shuffling again
* keyword play
* URL stream play
* playlist support
* Name/Artist guessing by path if no MP3 Tag info is available
* MP3 tag suport with guessing mechanism on missing tags

### Planned
* native MP3 streaming (depends on more recent libmp3/libout)
* serve stream (unlikely but desireable)

## cmixplay
ncurses based based player

### Parameters
* -d         : increase debug level
* -f         : disable fading
* -F         : enable fading
* -h <host>  : set hostname for remote play [127.0.0.1]
* -l         : run in local mode
* -p <port>  : set port for remote play [2347]
* -r         : act as client
* -v         : increase verbosity
* [path|URL] : path to the music files [play from db]

The debug level controls which messages are printed on the console. 
Verbosity controls the messages shown in the app.

### Control Keys
* [SPACE] - toggle pause
* s - stop playing
* q - quit player

on playlists:
* i - show path information
* n/[DOWN] - next title
* p/[UP] - previous title
* N - skip 5 titles forward
* P - skip 5 titles backward
* [LEFT] - rewind 64 frames
* [RIGHT] - skip 64 frames
* r - replay
* b - add to do-not-play list
* f - add to favoutites

## gmixplay
glade/gtk version of the player

### Additional Parameters
* -S         : run in fullscreen mode

