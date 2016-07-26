# mixplay
console based front-end to mpg123, planned to replace my old squeezebox/squeezeboxserver and act as a radio 
replacement to play background music but stay sleek enough to run on a mini ARM board.

So far it will scan a given directory for mp3 files, add them to an internal list and then play this list. 
The featurelist will probably not grow that much further as I just want to play music without the bloat of
a full featured management software.

### Features
* Skipping titles forward/backward
* Jumping forward backward during play
* favourites
* do-not-play lists
* shuffle play that avoids the same artist twice in a row
* shuffle will play each file before shuffling again
* keyword play
* URL stream play
* playlist support
* Name/Artist guessing by path (shall be replaced by MP3 tag reading)

### Parameters
* -d <file>  : List of names to exclude [mixplay.dnp]
* -f <file>  : List of favourites [mixplay.fav]
* -s <term>  : add search term (can be used multiple times)
* -S         : interactive search
* -R <talgp> : Set range (Title, Artist, aLbum, Genre, Path) [p]
* -p <file>  : use file as fuzzy playlist (party mode)
* -m         : disable shuffle mode on playlist
* -r         : disable repeat mode on playlist
* -v         : increase verbosity (just for debugging)
* -V         : print version*
* -h         : print this help*
* -C         : clear database and add titles anew *
* -A         : add new titles to the database *
* -D         : delete removed titles from the database *
* -T         : Tagrun, set MP3tags on all titles in the db *
* -F         : disable crossfading between songs
* -X         : print some database statistics*
* [path|URL] : path to the music files [.]
*  * these functions will not start the player

### Control Keys
* [SPACE] - toggle pause
* s - stop playing
* q - quite player

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

### Planned
* native MP3 streaming and tagging (lose mpg123 - low priority)
* interface revamp (dependant on actual hardware)
