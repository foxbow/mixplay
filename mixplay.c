#include "utils.h"
#include "ncutils.h"
#include "musicmgr.h"
#include "dbutils.h"

#include <getopt.h>
#include <signal.h>

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
	printf( "Usage: %s [-b <file>] [-f <file>] [-s <key>|-S] [-p <file>] [-m] [-r] [-v] [-T] [path|URL]\n", progname );
	printf( "-b <file>  : List of names to exclude\n" );
	printf( "-f <file>  : List of favourites\n" );
	printf( "-s <key>   : Search names like <key> (can be used multiple times)\n" );
	printf( "-S         : interactive search\n" );
	printf( "-m         : disable shuffle mode on playlist\n" );
	printf( "-r         : disable reapeat mode on playlist\n");
	printf( "-p <file>  : use file as fuzzy playlist (party mode)\n" );
	printf( "-v         : increase Verbosity (just for debugging)\n" );
	printf( "-T         : Tagrun, set MP3tags on all titles in the db\n" );
	printf( "[path|URL] : path to the music files [.]\n" );
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
	int p_status[2];
	int p_command[2];

	char line[LINE_BUFLEN];
	char status[LINE_BUFLEN] = "INIT";
	char tbuf[LINE_BUFLEN];
	char basedir[MAXPATHLEN];
	char dirbuf[MAXPATHLEN];
	char dbname[MAXPATHLEN] = "";
	char dnpname[MAXPATHLEN] = "";
	char wlname[MAXPATHLEN] = "";
	int key;
	char c;
	char *b;
	int mix = 1;
	int i;
	fd_set fds;
	struct timeval to;
	pid_t pid;
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

	FILE *fp=NULL;

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
	}
	else {
		printf( "%s does not exist.\n", dirbuf );
	}

	// if no basedir has been set, use the current dir as default
	if( 0 == strlen(basedir) ) {
		if (NULL == getcwd(basedir, MAXPATHLEN))
			fail("Could not get current dir!", "", errno);
	}

	// parse command line options
	while ((c = getopt(argc, argv, "mb:f:rvs:Sp:T")) != -1) {
		switch (c) {
		case 'v': // pretty useless in normal use
			incVerbosity();
		break;
		case 'm':
			mix = 0;
			break;
		case 'b':
			strcpy(dnpname, optarg);
			break;
		case 'f':
			strcpy(wlname, optarg);
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
			mix=0;
			usedb=0;
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
			// usually playlist should NOT be shuffled
			mix=0;
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
		if( dnpname[0] != '/' ) {
			strcpy( line, basedir );
			strcat( line, "/" );
			strcat( line, dnpname );
			strcpy( dnpname, line );
		}
	}
	dnplist=loadList( dnpname );

	// set default whitelist name
	if (0 == strlen( wlname) ) {
		strcpy( wlname, basedir );
		strcat( wlname, "/favourites.txt" );
	}
	// set given whitelist name
	else {
		if( wlname[0] != '/' ) {
			strcpy( line, basedir );
			strcat( line, "/" );
			strcat( line, wlname );
			strcpy( wlname, line );
		}
	}
	favourites=loadList( wlname );

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

		// create communication pipes
		pipe(p_status);
		pipe(p_command);

		pid = fork();
		if (0 > pid) {
			fail("could not fork", "", errno);
		}
		// child process
		if (0 == pid) {
			if (dup2(p_command[0], STDIN_FILENO) != STDIN_FILENO) {
				fail("Could not dup stdin for player", "", errno);
			}
			if (dup2(p_status[1], STDOUT_FILENO) != STDOUT_FILENO) {
				fail("Could not dup stdout for player", "", errno);
			}

			close(p_command[1]);
			close(p_status[0]);

			close(p_command[0]);
			close(p_status[1]);

			// Start mpg123 in Remote mode
			execlp("mpg123", "mpg123", "-R", "2>/dev/null", NULL);
			fail("Could not exec", "mpg123", errno);
		}

		else {
			close(p_command[0]);
			close(p_status[1]);

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
				FD_SET( p_status[0], &fds );
				to.tv_sec=1;
				to.tv_usec=0; // 1 sec
				i=select( FD_SETSIZE, &fds, NULL, NULL, &to );
				redraw=i?1:0;
				// Interpret keypresses
				if( FD_ISSET( fileno(stdin), &fds ) ) {
					key=getch();
					switch( key ){
						case ' ':
							write( p_command[1], "PAUSE\n", 7 );
						break;
						case 's':
							order=0;
							write( p_command[1], "STOP\n", 6 );
						break;
						case 'q':
							write( p_command[1], "QUIT\n", 6 );
							running=0;
						break;
					}
					if( !stream ) {
						switch( key ) {
							case KEY_DOWN:
							case 'n':
								order=1;
								write( p_command[1], "STOP\n", 6 );
							break;
							case KEY_UP:
							case 'p':
								order=-1;
								write( p_command[1], "STOP\n", 6 );
							break;
							case 'N':
								order=5;
								write( p_command[1], "STOP\n", 6 );
							break;
							case 'P':
								order=-5;
								write( p_command[1], "STOP\n", 6 );
							break;
							case KEY_LEFT:
								write( p_command[1], "JUMP -64\n", 10 );
							break;
							case KEY_RIGHT:
								write( p_command[1], "JUMP +64\n", 10 );
							break;
							case 'r':
								write( p_command[1], "JUMP 0\n", 8 );
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
								write( p_command[1], "STOP\n", 6 );
							break;
							case 'f': // toggles the favourite flag on a title
								if( !(current->flags & MP_FAV) ) {
									addToFile( wlname, strrchr( current->path, '/')+1 );
									current->flags|=MP_FAV;
								}
							break;
						}
					}
				}

				// Interpret mpg123 output and ignore invalid lines
				if( FD_ISSET( p_status[0], &fds ) &&
						( 3 < readline(line, 512, p_status[0]) ) ) {
					switch (line[1]) {
					int cmd, in, rem, q;
					case 'R': // startup
						current = root;
						sendplay(p_command[1], current);
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
						// Ignored
					break;
					case 'S':
						/* @S <a> <b> <c> <d> <e> <f> <g> <h> <i> <j> <k> <l>
						 * Status message after loading a song (stream info)
						 * a = mpeg type (string)
						 * b = layer (int)
						 * c = sampling frequency (int)
						 * d = mode (string)
						 * e = mode extension (int)
						 * f = framesize (int)
						 * g = stereo (int)
						 * h = copyright (int)
						 * i = error protection (int)
						 * j = emphasis (int)
						 * k = bitrate (int)
						 * l = extension (int)
						 */
						// ignore for now
						redraw=0;
						break;
					case 'F':
						/* @F <a> <b> <c> <d>
						 * Status message during playing (frame info)
						 * a = framecount (int)
						 * b = frames left this song (int)
						 * c = seconds (float)
						 * d = seconds left (float)
						 */
						if( tagrun ) {
							if( tagsync == 0 ){
								tagsync=1;
								if( current->played == 0 ) {
									dbSetTitle( db, current );
								}
								strcpy(status, "TAGGING");
								write( p_command[1], "STOP\n", 6 );
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
									sendplay( p_command[1], current );
								}
							}
							else {
								next = skipTitles( current, order );
								order=1;
								if ( ( !repeat && ( next == root ) ) || ( next == current ) ) {
									strcpy( status, "STOP" );
								}
								else {
									if( ( 1 == usedb ) &&
											( ( 0 == current->played ) || ( q >= 10 ) ) ) {
										current->played = current->played+1;
										dbSetTitle( db, current );
									}
									current=next;
									sendplay(p_command[1], current);
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
			kill(pid, SIGTERM);
			endwin();
		} // fork() parent
	}
	// root==NULL
	else {
		fail("No music found at", basedir, 0 );
	}
	return 0;
}
