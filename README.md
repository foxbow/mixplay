# mixplayd
the new heartpiece of the mixplay family. This is the manager and player that can run headless and will just play the shuffled Music content.

This is destined to run on a headless box and supply the stereo with a constant stream of music.

### Features
* Music database to avoid scanning on every start
* Skipping titles forward/backward
* Jumping forward backward during play
* favourites
* do-not-play lists
* shuffle play that avoids the same artist twice in a row
* shuffle will play each title before shuffling again
* URL stream play
* playlist support
* MP3 tag suport with guessing mechanism on missing tags

### Planned
* native MP3 streaming (depends on more recent libmp3/libout)
* serve stream (unlikely but desireable)

## mixplayd
demon version of the player. Communicates via HTTP/GET and JSON replies with clients. Also has a minimal web server to allow connection with a browser.

### Parameters
* -d         : increase console verbosity. Tells mixplayd to not detach and run in debug mode (this reads web files from the filesystem and does not use the hard coded versions.)
* -f         : disable fading
* -F         : enable fading
* -h <host>  : set hostname for remote play [127.0.0.1]
* -p <port>  : set port for remote play [2347]
* -v         : increase application verbosity
* [path|URL] : path to the music files [play from db]

# Developer info
The communication is wrapped in simple HTTP requests and JSON replies. The command structure looks as follows

cmd = 000F RRRR CCCC CCCC

### F - the 'fuzzy' flag for searching and marking

### R -  the range:
* 0x1 - title
* 0x2 - artist
* 0x3 - album
* 0x4 - genre
* 0x5 - display

### C - the actual commands:
* 0x00 - play/pause
* 0x01 - stop
* 0x02 - previous title
* 0x03 - next title
* 0x04 - start*
* 0x05 - replay
* 0x06 - change profile +arg <int>
* 0x07 - quit
* 0x08 - clean up database
* 0x09 - mark as favourite according to upper bytes
* 0x0a - mark as do not play according to upper bytes
* 0x0b - find double titles*
* 0x0c - insert title into playlist <key>
* 0x0d - increase volume
* 0x0e - decrease volume
* 0x0f - fast forward
* 0x10 - reverse rewind
* 0x11 - stop server*
* 0x12 - send database information
* 0x13 - search title +arg <string>
* 0x14 - append title to playlist <key>
* 0x15 - set volume +arg <0..100>
* 0x16 - create new profile +arg <string>
* 0x17 - play path/url <string>
* 0x18 - delete profile/channel <string>
* 0x19 - toggle edit mode
* 0x1a - wipe playlist
* 0x1b - save playlist <name>
* 0x1c - remove title from playlist <key>
* 0x1d - idle / max command*

args are set with the '?' operator.
Examples:
* set the volume to 50%: <server>:<port>/cmd/0015?50
* search for a title named like 'lov hurs': <server>:<port>/cmd/1113?lov+hurs
