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

## mixplay
console based based player based on mixplayd but not a front-end.

### Parameters
* -d <file>  : List of names to exclude [mixplay.dnp]
* -f <file>  : List of favourites [mixplay.fav]
* -s <term>  : add search term (can be used multiple times)
* -S         : interactive search
* -R <talgp> : Set range (Title, Artist, aLbum, Genre, Path) [p]
* -p <file>  : use file as fuzzy playlist (party mode)
* -m         : disable shuffle mode on playlist
* -r         : disable repeat mode on playlist
* -v         : increase verbosity
* -V         : print version*
* -h         : print this help*
* -C         : clear database and add titles anew *
* -A         : add new titles to the database *
* -D         : delete removed titles from the database *
* -F         : disable crossfading between songs
* -X         : print some database statistics*
* [path|URL] : path to the music files [play from db]
*  * these functions will not start the player

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
glade/gtk version of the player based on mixplayd but not a front-end, used as a lightweight desktop music player.

### Parameters
* -d         : raise debug level
* -v         : increase verbosity
* -h         : print this help*
* -F         : disable crossfading between songs
* [path|URL] : path to the music files [play from db]

