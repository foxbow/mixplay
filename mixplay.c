#include "utils.h"
#include "ncutils.h"
#include "musicmgr.h"
#include "dbutils.h"

#include <getopt.h>
#include <signal.h>
#include <sys/stat.h>

#ifndef VERSION
#define VERSION "dev"
#endif

#define LINE_BUFLEN 256

/**
 * print out CLI usage
 */
static void usage( char *progname ){
	printf( "%s-%s - console frontend to mpg123\n", progname, VERSION );
	printf( "Usage: %s [-d <file>] [-f <file>] [-s <key>|-S] [-p <file>] [-m] [-r] [-v] [-T] [-V] [-h] [path|URL]\n", progname );
	printf( "-d <file>  : List of names to exclude\n" );
	printf( "-f <file>  : List of favourites\n" );
	printf( "-s <key>   : Search names like <key> (can be used multiple times)\n" );
	printf( "-S         : interactive search\n" );
	printf( "-g <genre> : Search for titles in the given genre (multiple)\n");
	printf( "-G         : interactive genre search");
	printf( "-m         : disable shuffle mode on playlist\n" );
	printf( "-r         : disable reapeat mode on playlist\n");
	printf( "-p <file>  : use file as fuzzy playlist (party mode)\n" );
	printf( "-v         : increase Verbosity (just for debugging)\n" );
	printf( "-h         : print this help*\n");
	printf( "-V         : print version*\n");
	printf( "-C         : clear database and add titles anew *\n" );
	printf( "-A         : add new titles to the database *\n" );
	printf( "-D         : delete removed titles from the database *\n" );
	printf( "-T         : Tagrun, set MP3tags on all titles in the db *\n" );
	printf( "-F <time>  : start playing new song in the last <time> seconds of the current\n");
	printf( "[path|URL] : path to the music files [.]\n" );
	printf( " * these functions will not start the player\n");
	exit(0);
}

/**
 * Draw the application frame
 */
static void drawframe( struct entry_t *current, const char *status, int stream ) {
	int i, maxlen, pos;
	int row, col;
	int middle;
	char buff[LINE_BUFLEN];
	struct entry_t *runner;

	refresh();
	getmaxyx(stdscr, row, col);

	// Keep a minimum size to make sure
	if ((row > 6) && (col > 19)) {
		// main frame
		drawbox(1, 1, row - 2, col - 2);
		// outer frame
		mvhline(row - 1, 1, ' ', col);
		mvvline(1, col - 1, ' ', row);

		maxlen = col - 6;

		if( stream ) {
			middle=2;
		}
		else {
			middle=row/2;
		}

		// title
		dhline( middle-1, 1, col-3 );
		if( NULL != current ) {
			strip( buff, current->album, maxlen );
		} else {
			strip( buff, "mixplay "VERSION, maxlen );
		}
		pos = (col - (strlen(buff) + 2)) / 2;
		mvprintw(middle-1, pos, " %s ", buff);

		// Set the current playing title
		if( NULL != current ) {
			strip(buff, current->display, maxlen);
			if(current->flags & MP_FAV) {
				attron(A_BOLD);
			}

		}
		else {
			strcpy( buff, "---" );
		}
		setTitle(buff);

		pos = (col - strlen(buff)) / 2;
		mvhline(middle, 2, ' ', maxlen + 2);
		mvprintw(middle, pos, "%s", buff);
		attroff(A_BOLD);

		dhline( middle+1, 1, col-3 );

		// print the status
		strip(buff, status, maxlen);
		pos = (col - (strlen(buff) + 2)) / 2;
		mvprintw( row - 2, pos, " %s ", buff);

		// song list
		if( NULL != current ) {
			// previous songs
			runner=current->prev;
			for( i=middle-2; i>1; i-- ){
				if( NULL != runner ) {
					strip( buff, runner->display, maxlen );
					if(runner->flags & MP_FAV) {
						attron(A_BOLD);
					}
					runner=runner->prev;
				}
				else {
					strcpy( buff, "---" );
				}
				mvhline( i, 2, ' ', maxlen + 2);
				mvprintw( i, 3, "%s", buff);
				attroff(A_BOLD);
			}
			// past songs
			runner=current->next;
			for( i=middle+2; i<row-2; i++ ){
				if( NULL != runner ) {
					strip( buff, runner->display, maxlen );
					if(runner->flags & MP_FAV ) {
						attron(A_BOLD);
					}
					runner=runner->next;
				}
				else {
					strcpy( buff, "---" );
				}
				mvhline( i, 2, ' ', maxlen + 2);
				mvprintw( i, 3, "%s", buff);
				attroff(A_BOLD);
			}
		}
	}
	refresh();
}

/**
 * make mpeg123 play the given title
 */
static void sendplay( int fd, struct entry_t *song ) {
	char line[LINE_BUFLEN];
	strncpy( line, "load ", LINE_BUFLEN );
	strncat( line, song->path, LINE_BUFLEN );
	strncat( line, "\n", LINE_BUFLEN );
	write( fd, line, LINE_BUFLEN );
}

static void sendtag( int fd, struct entry_t *song ) {
	char line[LINE_BUFLEN];
	strncpy( line, "loadpaused ", LINE_BUFLEN );
	strncat( line, song->path, LINE_BUFLEN );
	strncat( line, "\n", LINE_BUFLEN );
	write( fd, line, strlen(line) );
}

/*
 *
 */
int main(int argc, char **argv) {
	/**
	 * CLI interface
	 */
	struct entry_t *root = NULL; // @ todo: have two lists, the database list and the playlist
	struct entry_t *current = NULL;
	struct entry_t *next = NULL;

	struct bwlist_t *dnplist=NULL;
	struct bwlist_t *favourites=NULL;
	struct bwlist_t *searchlist=NULL;
	struct bwlist_t *gsearchlist=NULL;

	// pipes to communicate with mpg123
	int p_status[2][2];
	int p_command[2][2];

	char line[LINE_BUFLEN];
	char status[LINE_BUFLEN] = "INIT";
	char tbuf[LINE_BUFLEN];
	char basedir[MAXPATHLEN];
	char dirbuf[MAXPATHLEN];
	char dbname[MAXPATHLEN] = "mixplay.db";
	char dnpname[MAXPATHLEN] = "mixplay.dnp";
	char favname[MAXPATHLEN] = "mixplay.fav";
	char config[MAXPATHLEN]="";
	int key;
	char c;
	char *b;
	int mix = 1;
	int i;
	fd_set fds;
	struct timeval to;
	pid_t pid[2];
	int redraw;
	// set when a stream is played
	int stream=0;
	// normal playing order
	int order=1;
	// mpg123 is up and running
	int running;
	int usedb=1;
	int search=0;
	int gsearch=0;
	int db=0;
	int tagrun=0;
	int repeat=1;
	int scan=0;
	int fade=0;
	int fdset=0;
// 	int hascfg=0;
	int vol=100;
	int intime=0;
	int dump=0;

	FILE *fp=NULL;
//	struct stat st;

	muteVerbosity();

	// load default config
	b=getenv("HOME");
	sprintf( config, "%s/.mixplay", b );
	fp=fopen(config, "r");
	if( NULL != fp ) {
		do {
			i=0;
			fgets( line, LINE_BUFLEN, fp );
			if( strlen(line) > 2 ) {
				line[strlen(line)-1]=0;
				switch( line[0] ) {
				case 'd':
					strncpy( dbname, line+1, MAXPATHLEN );
					break;
				case 's':
					strncpy( basedir, line+1, MAXPATHLEN );
					break;
				case '#':
					break;
				default:
					fail( "Config error:", line, -1 );
					break;
				}
			}
		} while( !feof(fp) );
//		hascfg=1;
	}

	// if no basedir has been set, use the current dir as default
	if( 0 == strlen(basedir) && ( NULL == getcwd( basedir, MAXPATHLEN ) ) ) {
		fail("Could not get current dir!", "", errno);
	}

	// parse command line options
	while ((c = getopt(argc, argv, "md:f:rvs:Sp:CADTF:VhXg:G")) != -1) {
		switch (c) {
		case 'v': // pretty useless in normal use
			incVerbosity();
		break;
		case 'm':
			mix = 0;
			break;
		case 'd':
			strcpy(dnpname, optarg);
			break;
		case 'f':
			strcpy(favname, optarg);
			break;
		case 'S':
			printf("Search: ");
			fflush(stdout);
			readline( line, MAXPATHLEN, fileno(stdin) );
			if( strlen(line) > 1 ) {
				search=1;
				searchlist=addToList( line, searchlist );
			}
			else {
				printf("-S: Ignoring less than three characters! (%s)", optarg);
				sleep(3);
				return -1;
			}
			break;
		case 's':
			if( strlen(optarg) > 2 ) {
				search=1;
				searchlist=addToList( optarg, searchlist );
			}
			else {
				printf("-s: Ignoring less than three characters! (%s)", optarg);
				sleep(3);
				return -1;
			}
			break;
		case 'G':
			printf("Genre: ");
			fflush(stdout);
			readline( line, MAXPATHLEN, fileno(stdin) );
			if( strlen(line) > 1 ) {
				gsearch=1;
				gsearchlist=addToList( line, gsearchlist );
			}
			else {
				printf("-G: Ignoring less than three characters! (%s)", optarg);
				sleep(3);
				return -1;
			}
			break;
		case 'g':
			if( strlen(optarg) > 2 ) {
				gsearch=1;
				gsearchlist=addToList( optarg, gsearchlist );
			}
			else {
				printf("-g: Ignoring less than three characters! (%s)", optarg);
				sleep(3);
				return -1;
			}
			break;
		case 'p':
			search=1;
			searchlist=loadList( optarg );
			break;
		case 'T':
			tagrun=1;
			break;
		case 'C':
			incVerbosity();
			scan|=3;
			break;
		case 'A':
			incVerbosity();
			scan|=2;
			break;
		case 'D':
			incVerbosity();
			scan|=4;
			break;
		case 'F':
			fade=atoi(optarg);
			break;
		case 'V':
			printf("mixplay-%s\n", VERSION );
			return 0;
			break;
		case 'h':
			usage(argv[0]);
			break;
		case 'X':
			dump=1;
			puts("-- Dumping Database statistics --");
			setVerbosity(2);
			break;
		default:
			printf("Unknown option -%c!\n\n", c );
			usage(argv[0]);
			break;
		}
	}

	if( tagrun ) {
		usedb=1;
		mix=0;
		repeat=0;
	}

	// parse additional argument
	if (optind < argc) {
		if( isURL( argv[optind] ) ) {
			mix=0;		// mixing a stream is a bad idea
			usedb=0;	// a stream needs no db
			repeat=0;	// @todo: repeat may even be useful on stream disconnect
			fade=0;		// fading is really dumb
			stream=1;
			line[0]=0;
			if( endsWith( argv[optind], "m3u" ) ) {
				strcpy( line, "@" );
			}

			strncat( line, argv[optind], MAXPATHLEN );
			root=insertTitle( root, line );
			root->display[0]=0; // hide URL from display
		}
		else if( endsWith( argv[optind], ".mp3" ) ) {
			// play single song...
			usedb=0;
			mix=0;
			repeat=0;
			root=insertTitle( root, argv[optind] );
		}
		else if ( endsWith( argv[optind], "m3u" ) ) {
			usedb=0;
			mix=0;
			root=loadPlaylist( argv[optind] );
			if( NULL != strrchr( argv[optind], '/' ) ) {
				strcpy(basedir, argv[optind]);
				i=strlen(basedir);
				while( basedir[i] != '/' ) i--;
				basedir[i]=0;
				chdir(basedir);
			}
		}
		else if ( endsWith( argv[optind], ".db" ) ) {
			usedb=1;
			strncpy( dbname, argv[optind], MAXPATHLEN );
		}
		else {
			if( !scan ) usedb=0;
			if( argv[optind][0] != '/' ) {
				snprintf( dirbuf, MAXPATHLEN, "%s/%s", basedir, argv[optind] );
				strncpy( basedir, dirbuf, MAXPATHLEN );
			}
			else {
				strncpy( basedir, argv[optind], MAXPATHLEN );
			}
		}
	}

	if (basedir[strlen(basedir) - 1] == '/')
		basedir[strlen(basedir) - 1] = 0;

	if( strchr( dbname, '/' ) == NULL ) {
		strncpy( dirbuf, dbname, MAXPATHLEN );
		snprintf( dbname, MAXPATHLEN, "%s/%s", basedir, dirbuf );
	}

	// scanformusic functionality
	if( scan ) {
		if ( ( scan & 1 ) && ( 0 != remove(dbname) ) ) {
			fail("Cannot delete", dbname, errno );
		}
		if( scan & 2 ) {
			dbAddTitles( dbname, basedir );
		}
		if( scan & 4 ) {
			dbCheckExist( dbname );
		}

		if( !tagrun ) return 0;
	}

	if( tagrun && !usedb ) {
		fail( "Tagrun needs a database!", "", F_FAIL );
	}

	if( strchr( dnpname, '/' ) == NULL ) {
		strcpy( line, basedir );
		strcat( line, "/" );
		strcat( line, dnpname );
		strcpy( dnpname, line );
	}
	dnplist=loadList( dnpname );

	// set default favourites name
	if( strchr( favname, '/' ) == NULL  ) {
		strcpy( line, basedir );
		strcat( line, "/" );
		strcat( line, favname );
		strcpy( favname, line );
	}
	favourites=loadList( favname );

	// load and prepare titles
	if( NULL == root ) {
		if( usedb ) {
			dbOpen( &db, dbname );
			root=dbGetMusic( db );
			dbClose( &db );
		} else {
			root=recurse(basedir, NULL, basedir);
			root=root->next;
		}
		if( ! tagrun ) {
			root=useDNPlist( root, dnplist );
			applyFavourites( root, favourites );
		}

		if(search){
			root=searchList(root, searchlist);
		}

		if(gsearch) {
			root=gsearchList( root, gsearchlist );
		}
	}

	if( dump ) {
		unsigned long maxplayed=0;
		unsigned long minplayed=-1;
		unsigned long pl=0;

		current=root;
		do {
			if( current->played < minplayed ) minplayed=current->played;
			if( current->played > maxplayed ) maxplayed=current->played;
			current=current->next;
		} while( current != root );

		for( pl=minplayed; pl <= maxplayed; pl++ ) {
			unsigned long pcount=0;
			do {
				if( current->played == pl ) pcount++;
				current=current->next;
			} while( current != root );
			printf("%5li times played: %5li titles\n", pl, pcount );
		}
		puts("");
		return 0;
	}

	// No else as the above calls may return NULL!
	// prepare playing the titles
	if (NULL != root) {
		if (mix && !tagrun) {
			root=shuffleTitles(root);
		}

		// start the player processes
		for( i=0; i <= (fade?1:0); i++ ) {
			// create communication pipes
			pipe(p_status[i]);
			pipe(p_command[i]);

			pid[i] = fork();
			if (0 > pid[i]) {
				fail("could not fork", "", errno);
			}
			// child process
			if (0 == pid[i]) {
				if (dup2(p_command[i][0], STDIN_FILENO) != STDIN_FILENO) {
					fail("Could not dup stdin for player", "", errno);
				}
				if (dup2(p_status[i][1], STDOUT_FILENO) != STDOUT_FILENO) {
					fail("Could not dup stdout for player", "", errno);
				}

				// this process needs no pipes
				close(p_command[i][0]);
				close(p_command[i][1]);
				close(p_status[i][0]);
				close(p_status[i][1]);

				// Start mpg123 in Remote mode
				execlp("mpg123", "mpg123", "-R", "2>/dev/null", NULL);
				// execlp("mpg123", "mpg123", "-R", "--remote-err", NULL); // breaks the reply parsing!
				fail("Could not exec", "mpg123", errno);
			}
			close(p_command[i][0]);
			close(p_status[i][1]);
		}

		running=1;

		// Start curses mode
		initscr();
		curs_set(0);
		cbreak();
		keypad(stdscr, TRUE);
		noecho();
		drawframe( NULL, status, stream );

		if( usedb ) dbOpen( &db, dbname );
		while (running) {
			FD_ZERO( &fds );
			FD_SET( fileno(stdin), &fds );
			FD_SET( p_status[0][0], &fds );
			if( fade != 0 ) FD_SET( p_status[1][0], &fds );
			to.tv_sec=1;
			to.tv_usec=0; // 1 sec
			i=select( FD_SETSIZE, &fds, NULL, NULL, &to );
			if( i>0 ) redraw=1;
			// Interpret keypresses
			if( FD_ISSET( fileno(stdin), &fds ) ) {
				key=getch();
				switch( key ){
					case ' ':
						write( p_command[fdset][1], "PAUSE\n", 7 );
					break;
					case 's':
						order=0;
						write( p_command[fdset][1], "STOP\n", 6 );
					break;
					case 'q':
						write( p_command[fdset][1], "QUIT\n", 6 );
						running=0;
					break;
				}
				if( !stream ) {
					switch( key ) {
						case KEY_DOWN:
						case 'n':
							order=1;
							write( p_command[fdset][1], "STOP\n", 6 );
						break;
						case KEY_UP:
						case 'p':
							order=-1;
							write( p_command[fdset][1], "STOP\n", 6 );
						break;
						case 'N':
							order=5;
							write( p_command[fdset][1], "STOP\n", 6 );
						break;
						case 'P':
							order=-5;
							write( p_command[fdset][1], "STOP\n", 6 );
						break;
						case KEY_LEFT:
							write( p_command[fdset][1], "JUMP -64\n", 10 );
						break;
						case KEY_RIGHT:
							write( p_command[fdset][1], "JUMP +64\n", 10 );
						break;
						case 'r':
							write( p_command[fdset][1], "JUMP 0\n", 8 );
						break;
						case 'b':
							addToFile( dnpname, strrchr( current->path, '/')+1 );
							current=removeTitle( current );
							if( NULL != current->prev ) {
								current=current->prev;
							}
							else {
								fail("Broken link", current->path, F_FAIL);
							}
							order=1;
							write( p_command[fdset][1], "STOP\n", 6 );
						break;
						case 'f': // toggles the favourite flag on a title
							if( !(current->flags & MP_FAV) ) {
								addToFile( favname, strrchr( current->path, '/')+1 );
								current->flags|=MP_FAV;
							}
						break;
					}
				}
			}
			// drain inactive player
			if( fade && FD_ISSET( p_status[fdset?0:1][0], &fds ) ) {
				readline(line, 512, p_status[fdset?0:1][0]);
			}

			// Interpret mpg123 output and ignore invalid lines
			if( FD_ISSET( p_status[fdset][0], &fds ) &&
					( 3 < readline(line, 512, p_status[fdset][0]) ) ) {
				switch (line[1]) {
				int cmd=0, rem=0, q=0;
				case 'R': // startup
					current = root;
					if( tagrun ) {
						strcpy( status, "TAGGING" );
						sendtag(p_command[fdset][1], current);
					}
					else {
						sendplay(p_command[fdset][1], current);
					}
				break;
				case 'I': // ID3 info
					/* @I ID3.2.year:2016
					 * @I ID3.2.comment:http://www.faderhead.com
					 * @I ID3.2.genre:EBM / Electronic / Electro
					 *
					 * @I <a>
					 * Status message after loading a song (no ID3 song info)
					 * a = filename without path and extension
					 */
					// ICY stream info
					if( NULL != strstr( line, "ICY-" ) ) {
						if( NULL != strstr( line, "ICY-NAME: " ) ) {
							strip( current->album, line+13, NAMELEN );
						}
						if( NULL != ( b = strstr( line, "StreamTitle") ) ) {
							b = b + 13;
							*strchr(b, '\'') = '\0';
							if( strlen(current->display) != 0 ) {
								insertTitle( current, current->display );
							}
							strip(current->display, b, MAXPATHLEN );
						}
					}
					// standard mpg123 info
					else {
						if (NULL != (b = strstr(line, "title:"))) {
							strip(current->title, b + 6, NAMELEN );
							snprintf( current->display, MAXPATHLEN, "%s - %s",
									current->artist, current->title );
						}
						// line starts with 'Artist:' this means we had a 'Title:' line before
						else if (NULL != (b = strstr(line, "artist:"))) {
							strip(current->artist, b + 7, NAMELEN );
							snprintf( current->display, MAXPATHLEN, "%s - %s",
									current->artist, current->title );
						}
						// Album
						else if (NULL != (b = strstr(line, "album:"))) {
							strip( current->album, b + 6, NAMELEN );
						}
						else if( NULL != (b = strstr( line, "genre:" ) ) ) {
							strip( current->genre, b+6, NAMELEN );
						}
					}
					redraw=1;
				break;
				case 'T': // TAG reply
					redraw=0;
					if (NULL != (b = strstr(line, "title:"))) {
						strip(current->title, b + 6, NAMELEN );
						snprintf( current->display, MAXPATHLEN, "%s - %s",
								current->artist, current->title );
					}
					// line starts with 'Artist:' this means we had a 'Title:' line before
					else if (NULL != (b = strstr(line, "artist:"))) {
						strip(current->artist, b + 7, NAMELEN );
						snprintf( current->display, MAXPATHLEN, "%s - %s",
								current->artist, current->title );
					}
					// Album
					else if (NULL != (b = strstr(line, "album:"))) {
						strip( current->album, b + 6, NAMELEN );
					}
					else if( NULL != (b = strstr( line, "genre:" ) ) ) {
						strip( current->genre, b+6, NAMELEN );
					}
					else if ( '}' == line[3] ) {
						dbSetTitle( db, current );
						current=current->next;
						if( current == root ) {
							strcpy( status, "DONE" );
							redraw=1;
						}
						else {
							sendtag(p_command[fdset][1], current);
						}
					}
				break;
				case 'J': // JUMP reply
					redraw=0;
				break;
				case 'S': // Status message after loading a song (stream info)
					redraw=0;
				break;
				case 'F': // Status message during playing (frame info)
					/* $1   = framecount (int)
					 * $2   = frames left this song (int)
					 * in  = seconds (float)
					 * rem = seconds left (float)
					 */
					b=strrchr( line, ' ' );
					rem=atoi(b);
					*b=0;
					b=strrchr( line, ' ' );
					intime=atoi(b);
					// file play
					if( 0 != rem ) {
						q=(30*intime)/(rem+intime);
						memset( tbuf, 0, LINE_BUFLEN );
						for( i=0; i<30; i++ ) {
							if( i < q ) tbuf[i]='=';
							else if( i == q ) tbuf[i]='>';
							else tbuf[i]=' ';
						}
						sprintf(status, "%i:%02i [%s] %i:%02i", intime/60, intime%60, tbuf, rem/60, rem%60 );
						if( ( fade != 0 ) && ( rem <= fade ) ) {
							current->played++;
							dbSetTitle( db, current );
							next = skipTitles( current, order );
							if ( ( !repeat && ( next == root ) ) || ( next == current ) ) {
								strcpy( status, "STOP" );
							}
							else {
								current=next;
								// swap player
								fdset=fdset?0:1;
								vol=0;
								write( p_command[fdset][1], "volume 0\n", 9 );
								sendplay(p_command[fdset][1], current);
							}
						}
						if( vol < 100 ) {
							vol++;
							snprintf( line, LINE_BUFLEN, "volume %i\n", vol );
							write( p_command[fdset][1], line, strlen(line) );
						}

					}
					// stream play
					else {
						if( intime/60 < 60 ) {
							sprintf(status, "%i:%02i PLAYING", intime/60, intime%60 );
						}
						else {
							sprintf(status, "%i:%02i:%02i PLAYING", intime/3600, (intime%3600)/60, intime%60 );
						}
					}
					redraw=1;
				break;
				case 'P': // Player status
					cmd = atoi(&line[3]);
					switch (cmd) {
					case 0:
						// update playcount after 15s
						// only happens on non fading title change
						if ( (intime > 15 ) && ( 1 == usedb ) ) {
							current->played = current->played+1;
							dbSetTitle( db, current );
						}
						next = skipTitles( current, order );
						order=1;
						if ( ( !repeat && ( next == root ) ) || ( next == current ) ) {
							strcpy( status, "STOP" );
						}
						else {
							current=next;
							sendplay(p_command[fdset][1], current);
						}
						break;
					case 1:
						if( tagrun ) {
							redraw=1;
							write( p_command[fdset][1], "tag\n", 4 );
						}
						else {
							strcpy( status, "PAUSE" );
						}
						break;
					case 2:
						strcpy( status, "PLAYING" );
						break;
					default:
						sprintf( status, "Unknown status %i!", cmd);
						drawframe( current, status, stream );
						sleep(1);
						break;
					}
					redraw=1;
					break;
				case 'V': // volume reply
					redraw=0;
				break;
				case 'E':
					sprintf( status, "ERROR: %s", line);
					drawframe( current, status, stream );
					sleep(1);
				break;
				default:
					if( !tagrun ) {
						sprintf( status, "MPG123 : %s", line);
						drawframe( current, status, stream );
						sleep(1);
					}
				break;
				} // case()
			} // fgets() > 0
			if( redraw ) drawframe( current, status, stream );
		} // while(running)
		if( usedb ) dbClose( &db );
		kill(pid[0], SIGTERM);
		if( fade ) kill( pid[1], SIGTERM );
		endwin();
	} // root==NULL
	else {
		fail("No music found at", basedir, 0 );
	}
	return 0;
}
