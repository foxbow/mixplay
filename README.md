# mixplay
console based front-end to mpg123, planned to replace my old squeezebox/squeezeboxserver and act as a radio 
replacement to play background music but stay sleek enough to run on a mini ARM board.

So far it will scan a given directory for mp3 files, add them to an internal list and then play this list. 
The featurelist will probably not grow that much further as I just want to play music without the bloat of
a full featured management software.

### Features
* Skipping titles forward/backward
* Jumping forward backward during play
* add favourites
* blacklisting
* shuffle play that avoids the same artist twice in a row
* shuffle will play each file before shuffling again
* keyword play
* URL stream play
* playlist support
* Name/Artist guessing by path (shall be replaced by MP3 tag reading)

### Planned
* Database support to avoid rescan on each and every start
* MP3 Tag reading
* native MP3 streaming (lose mpg123 - low priority)
* interface revamp (dependant on actual hardware)
