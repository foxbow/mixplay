# Developer info
The communication is wrapped in simple HTTP requests and JSON replies. The command structure looks as follows

cmd = 0MSR RRRR CCCC CCCC

### M - the 'mix' flag for playlists and the 'fuzzy' flag for searching and marking

### S - 'substring' marker for search.

### R -  the range:
* 0x01 - title
* 0x02 - artist
* 0x04 - album
* 0x08 - display
* 0x10 - genre

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
* 0x0c - insert searchresults into playlist
* 0x0d - increase volume
* 0x0e - decrease volume
* 0x0f - fast forward
* 0x10 - reverse rewind
* 0x11 - move title in playlist to next
* 0x12 - send database information
* 0x13 - search title +arg <string>
* 0x14 - append searchresults to playlist
* 0x15 - set volume +arg <0..100>
* 0x16 - create new profile +arg <string>
* 0x17 - play path/url <string>
* 0x18 - delete profile/channel <string>
* 0x19 - toggle edit mode
* 0x1a - remove <entry> from dnplist
* 0x1b - remove <entry> from favlist
* 0x1c - remove title from playlist <key>
* 0x1d - toggle mute
* 0x1e - toggle favplay
* 0x1f - idle / max command*

args are set with the '?' operator.
Examples:
* set the volume to 50%: <server>:<port>/cmd/0015?50
* search for a title named like 'lov hurs': <server>:<port>/cmd/1113?lov+hurs
