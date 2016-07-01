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
 * mb:w:rvs
 */
static void usage( char *progname ){
	printf( "%s - console frontend to mpg123\n", progname );
	printf( "Usage: %s [-d <file>] [-f <file>] [-s <key>|-S] [-p <file>] [-m] [-r] [-v] [-T] [path|URL]\n", progname );
	printf( "-d <file>  : List of names to exclude\n" );
	printf( "-f <file>  : List of favourites\n" );
	printf( "-s <key>   : Search names like <key> (can be used multiple times)\n" );
	printf( "-S         : interactive search\n" );
	printf( "-m         : disable shuffle mode on playlist\n" );
	printf( "-r         : disable reapeat mode on playlist\n");
	printf( "-p <file>  : use file as fuzzy playlist (party mode)\n" );
	printf( "-v         : increase Verbosity (just for debugging)\n" );
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

int main(int argc, char **argv) {
	/**
	 * CLI interface
	 */
	struct entry_t *root = NULL;
	struct entry_t *current = NULL;
	struct entry_t *next = NULL;

	struct bwlist_t *dnplist=NULL;
	struct bwlist_t *favourites=NULL;
	struct bwlist_t *searchlist=NULL;

	// pipes to communicate with mpg123
	int p_status[2][2];
	int p_command[2][2];

	char line[LINE_BUFLEN];
	char status[LINE_BUFLEN] = "INIT";
	char tbuf[LINE_BUFLEN];
	char basedir[MAXPATHLEN];
	char dirbuf[MAXPATHLEN];
	char dbname[MAXPATHLEN] = "";
	char dnpname[MAXPATHLEN] = "";
	char favname[MAXPATHLEN] = "";
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
	int db=0;
	int tagrun=0;
	int repeat=1;
	int tagsync=0;
	int scan=0;
	int fade=0;
	int fdset=0;
	int hascfg=0;

	FILE *fp=NULL;
	struct stat st;

	muteVerbosity();

	// load default config
	b=getenv("HOME");
	sprintf( dirbuf, "%s/.mixplay", b );
	fp=fopen(dirbuf, "r");
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
		hascfg=1;
	}

	// if no basedir has been set, use the current dir as default
	if( 0 == strlen(basedir) && ( NULL == getcwd( basedir, MAXPATHLEN ) ) ) {
		fail("Could not get current dir!", "", errno);
	}

	// parse command line options
	while ((c = getopt(argc, argv, "md:f:rvs:Sp:CADTF:")) != -1) {
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
				puts("Ignoring less than three characters!");
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
				puts("Ignoring less than three characters!");
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
			scan|=3;
			break;
		case 'A':
			scan|=2;
			break;
		case 'D':
			scan|=4;
			break;
		case 'F':
			fade=atoi(optarg);
			break;
		default:
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
			usedb=0;
			strncpy( basedir, argv[optind], MAXPATHLEN );
			if (basedir[strlen(basedir) - 1] == '/')
				basedir[strlen(basedir) - 1] = 0;
		}
	}

	if( strchr( dbname, '/' ) == NULL ) {
		strncpy( dirbuf, dbname, MAXPATHLEN );
		snprintf( dbname, MAXPATHLEN, "%s/%s", basedir, dbname );
	}

	if( usedb && !hascfg ) {
		printf("No default configuration found!\n");
		printf("It will be set up now\n");
		while(1){
			printf("Default music directory:"); fflush(stdout);
			memset( basedir, 0, MAXPATHLEN );
			fgets( basedir, MAXPATHLEN, stdin );
			if( basedir[0] != '/' ) { // we want absolute paths
				snprintf( line, LINE_BUFLEN, "%s/%s", b, basedir );
				strncpy( basedir, line, MAXPATHLEN );
			}
			if( stat( basedir, &st ) ) {
				printf("Cannot access %s!\n", basedir );
			}
			else if( S_ISDIR( st.st_mode ) ){
				break;
			}
			else {
				printf("%s is not a directory!\n", basedir );
			}
		}
		fp=fopen(dirbuf, "w");
		if( NULL != fp ) {
			fputs( "# mixplay configuration", fp );
			fputc( 's', fp );
			fputs( basedir, fp );
			fclose(fp);
			fail("Done.", "", F_FAIL );
		}
		else {
			fail("Could not open", dirbuf, errno );
		}
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

	// set default dnplist name
	if (0 == strlen( dnpname) ) {
		strcpy( dnpname, basedir );
		strcat( dnpname, "/default.dnp" );
	}
	// load given dnplist
	else {
		if( strchr( dnpname, '/' ) == NULL ) {
			strcpy( line, basedir );
			strcat( line, "/" );
			strcat( line, dnpname );
			strcpy( dnpname, line );
		}
	}
	dnplist=loadList( dnpname );

	// set default favourites name
	if (0 == strlen( favname) ) {
		strcpy( favname, basedir );
		strcat( favname, "/favourites.txt" );
	}
	// set given favourites name
	else {
		if( strchr( favname, '/' ) == NULL  ) {
			strcpy( line, basedir );
			strcat( line, "/" );
			strcat( line, favname );
			strcpy( favname, line );
		}
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
			if( FD_ISSET( p_status[fdset?0:1][0], &fds ) ) {
				readline(line, 512, p_status[fdset?0:1][0]);
			}

			// Interpret mpg123 output and ignore invalid lines
			if( FD_ISSET( p_status[fdset][0], &fds ) &&
					( 3 < readline(line, 512, p_status[fdset][0]) ) ) {
				switch (line[1]) {
				int cmd, in, rem, q;
				case 'R': // startup
					current = root;
					sendplay(p_command[fdset][1], current);
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
					}
					redraw=1;
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
					if( tagrun ) {
						if( tagsync == 0 ){
							tagsync=1;
							if( current->played == 0 ) {
								dbSetTitle( db, current );
							}
							strcpy(status, "TAGGING");
							write( p_command[fdset][1], "STOP\n", 6 );
						}
					}
					else {
						b=strrchr( line, ' ' );
						rem=atoi(b);
						*b=0;
						b=strrchr( line, ' ' );
						in=atoi(b);
						// file play
						if( 0 != rem ) {
							q=(30*in)/(rem+in);
							memset( tbuf, 0, LINE_BUFLEN );
							for( i=0; i<30; i++ ) {
								if( i < q ) tbuf[i]='=';
								else if( i == q ) tbuf[i]='>';
								else tbuf[i]=' ';
							}
							sprintf(status, "%i:%02i [%s] %i:%02i", in/60, in%60, tbuf, rem/60, rem%60 );
							if( ( fade != 0 ) && ( rem <= fade ) ) {
								next = skipTitles( current, order );
								if ( ( !repeat && ( next == root ) ) || ( next == current ) ) {
									strcpy( status, "STOP" );
								}
								else {
									// note: no check for how long the title has played
									// because when a title is skipped it should move to
									// the end of the queue
									if ( 1 == usedb ) {
										current->played = current->played+1;
										dbSetTitle( db, current );
									}
									current=next;
									// swap player
									fdset=fdset?0:1;
									sendplay(p_command[fdset][1], current);
								}
							}
						}
						// stream play
						else {
							if( in/60 < 60 ) {
								sprintf(status, "%i:%02i PLAYING", in/60, in%60 );
							}
							else {
								sprintf(status, "%i:%02i:%02i PLAYING", in/3600, (in%3600)/60, in%60 );
							}
						}
					}
					redraw=1;
					break;
				case 'P': // Player status
					cmd = atoi(&line[3]);
					switch (cmd) {
					case 0:
						if( tagrun ) {
							if( current->played == 0 ) {
								current->played=1;
								dbSetTitle( db, current );
							}
							current=current->next;
							while( ( current->played > 0 ) && ( current != root ) ) {
								current=current->next;
							}
							if( current == root ) {
								strcpy( status, "STOP" );
							}
							else {
								tagsync=0;
								sendplay( p_command[fdset][1], current );
							}
						}
						else {
							next = skipTitles( current, order );
							order=1;
							if ( ( !repeat && ( next == root ) ) || ( next == current ) ) {
								strcpy( status, "STOP" );
							}
							else {
								// note: no check for how long the title has played
								// because when a title is skipped it should move to
								// the end of the queue
								if ( 1 == usedb ) {
									current->played = current->played+1;
									dbSetTitle( db, current );
								}
								current=next;
								sendplay(p_command[fdset][1], current);
							}
						}
						break;
					case 1:
						strcpy( status, "PAUSE" );
						break;
					case 2:
						strcpy( status, "PLAYING" );
						break;
					default:
						sprintf( status, "Unknown status %i!", cmd);
						drawframe( current, status, stream );
						sleep(1);
					}
					redraw=1;
					break;
				case 'E':
					sprintf( status, "ERROR: %s", line);
					drawframe( current, status, stream );
					sleep(1);
					break;
				default:
					sprintf( status, "MPG123 : %s", line);
					drawframe( current, status, stream );
					sleep(1);
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
