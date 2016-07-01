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

### Room for improvement
* MP3 Tag reading - now done by startign each song and setting the tag data

### Planned
* native MP3 streaming (lose mpg123 - low priority)
* interface revamp (dependant on actual hardware)
