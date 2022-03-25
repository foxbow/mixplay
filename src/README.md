# Developer info
The communication is wrapped in simple HTTP requests and JSON replies. The command structure looks as follows

cmd = 0MSR RRRR 000C CCCC

### M - the 'mix' flag for playlists and the 'fuzzy' flag for searching and marking

### S - 'substring' marker for search.

### R -  the range:
* 0x01 - genre
* 0x02 - artist
* 0x04 - album
* 0x08 - title
* 0x10 - display
* 0x1f - path

### C - the actual commands/states:
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
* 0x1f - reset player
* 0x20 - player is in pause state
* 0x21 - clone current profile into <arg>
* 0x22 - idle / max command*

args are set with the '?' operator.

## mpclient API
mpclient.h describes a simple API to do basic communication with the server.
See mixplay-hid.c and mixplay-scr.c for practical examples.

* int32_t setMPPort(int32_t port);
  set the port number to connect to (default: 2347)
  returns 0 on success and -1 if the portnumber is out of range

* int32_t setMPHost(const char *host);
  set the hostname/IP to connect to (default: 127.0.0.1)
  returns 0 on success and -1 of the name is too long

* const char *getMPHost(void);
  returns the current hostname/IP to connect to

* int getConnection(void);
  opens a connection to the server
  returns -2 if the hostname cannot be resolved, -1 if the connection fails
  and the filedescriptor on success

* int32_t sendCMD(mpcmd_t cmd, const char *arg);
  sends 'cmd' and 'arg' top the server. arg may be NULL if no argument is
  needed
  returns 1 on success, 0 if the server is busy and -1 on failure

* int32_t getCurrentTitle(char *title, uint32_t tlen);
  fetches the current title in the format "<artist> - <title>" and stores the
  first 'tlen' characters in 'title'
  returns the actual length on success and -1 on failure

* jsonObject *getStatus(int32_t flags);
  gets the current player status according to the flags
  flags can be or'ed:
  * MPCOMM_STAT   get simple player status
  * MPCOMM_TITLES add full title/playlist info
  * MPCOMM_LISTS  add DNP and favlists
  * MPCOMM_CONFIG add immutable configuration
  returns NULL on error and a jsonObject tree with the requested information
  on success

* int32_t jsonGetTitle(jsonObject * jo, const char *key, mptitle_t * title);
  extracts a title from the given jsonObject tree. If the key cannot be
  resolved dummy title information will be set to 'Mixplay'
  returns 0 on failure and 1 on success
