#include "utils.h"
#include "ncutils.h"
#include "musicmgr.h"

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
	printf( "Usage: %s [-b <file>] [-w <file>] [-m] [-s <key>] -S -v  [path|URL]\n", progname );
	printf( "-b <file>  : Blacklist of names to exclude\n" );
	printf( "-w <file>  : Whitelist of names to include\n" );
	printf( "-m         : Mix, enable shuffle mode on playlist\n" );
	printf( "-r         : Repeat playlist\n");
	printf( "-v         : increase Verbosity (just for debugging)\n" );
	printf( "-s <key>   : Search names like <key> (can be used multiple times)\n" );
	printf( "-S         : interactive search\n" );
	printf( "[path|URL] : path to the music files [.]\n" );
	exit(0);
}

/**
 * Draw the application frame
 */
static void drawframe(char *station, struct entry_t *current, const char *status, int stream ) {
	int i, maxlen, pos, rows;
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
		strncpy(buff, station, maxlen);
		pos = (col - (strlen(buff) + 2)) / 2;
		mvprintw(middle-1, pos, " %s ", buff);

		// Set the current playing title
		if( NULL != current ) {
			strip(buff, current->display, maxlen);
			if(current->rating) {
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
		strncpy(buff, status, maxlen);
		pos = (col - (strlen(buff) + 2)) / 2;
		mvprintw( row - 2, pos, " %s ", buff);

		// song list
		if( NULL != current ) {
			// previous songs
			runner=current->prev;
			for( i=middle-2; i>1; i-- ){
				if( NULL != runner ) {
					strip( buff, runner->display, maxlen );
					if(runner->rating) {
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
					if(runner->rating) {
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

static void sendplay( int fd, struct entry_t *song ) {
	char line[LINE_BUFLEN];

	strncpy( line, "load ", LINE_BUFLEN );
	if( strlen(song->path) != 0 ){
		strncat( line, song->path, LINE_BUFLEN );
		strncat( line, "/", LINE_BUFLEN );
	}
	strncat( line, song->name, LINE_BUFLEN );
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

	// pipes to communicate with mpg123
	int p_status[2];
	int p_command[2];

	char line[LINE_BUFLEN];
	char status[LINE_BUFLEN] = "INIT";
	char tbuf[LINE_BUFLEN];
	char station[LINE_BUFLEN] = "mixplay "VERSION;
	char basedir[MAXPATHLEN];
	char dirbuf[MAXPATHLEN];
	char dbname[MAXPATHLEN] = "";
	char blname[MAXPATHLEN] = "";
	char wlname[MAXPATHLEN] = "";
	int key;
	char c;
	char *b;
	int mix = 0;
	int i, cnt = 1;
	fd_set fds;
	struct timeval to;
	pid_t pid;
	int redraw;
	// set when a stream is played
	int stream=0;
	// no repeat
	int repeat = 0;
	// normal playing order
	int order=1;
	// mpg123 is up and running
	int running;

	FILE *fp=NULL;

	// load config
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
				case '+':
					i=1;
					break;
				case '-':
					i=-1;
					break;
				case '#':
					break;
				case 'b':
					strncpy( blname, line+1, MAXPATHLEN );
					break;
				case 'w':
					strncpy( wlname, line+1, MAXPATHLEN );
					break;
				default:
					fail( "Config error:", line, -1 );
					break;
				}
				if( 0 != i ){
					if( startsWith( &line[1], "mix" ) ) mix=(i==-1?0:1);
					else if( startsWith( &line[1], "repeat" ) ) repeat=(i==-1?0:1);
					else fail( "Unknown keyword:", line, -1 );
				}
			}
		} while( !feof(fp) );
	}
	else {
		printf( "%s dies not exist.\n", dirbuf );
	}

	if( 0 == strlen(basedir) ) {
		if (NULL == getcwd(basedir, MAXPATHLEN))
			fail("Could not get current dir!", "", errno);
	}

	while ((c = getopt(argc, argv, "mb:w:rvs:S")) != -1) {
		switch (c) {
		case 'v': // not documented and pretty useless in normal use
			incVerbosity();
		break;
		case 'm':
			mix = 1;
			break;
		case 'b':
			strcpy(blname, optarg);
			break;
		case 'w':
			strcpy(wlname, optarg);
			break;
		case 'r':
			repeat = 1;
			break;
		case 'S':
			printf("Search: ");
			fflush(stdout);
			readline( line, MAXPATHLEN, fileno(stdin) );
			if( strlen(line) > 2 ) {
				addToWhitelist( line );
			}
			else {
				puts("Ignoring less than three characters!");
				sleep(3);
				return -1;
			}
			break;
		case 's':
			if( strlen(optarg) > 2 ) {
				addToWhitelist( optarg );
			}
			else {
				puts("Ignoring less than three characters!");
				sleep(3);
				return -1;
			}
			break;
		default:
			usage(argv[0]);
			break;
		}
	}

	if (optind < argc) {
		if( isURL( argv[optind] ) ) {
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
			root=insertTitle( root, argv[optind] );
		}
		else if ( endsWith( argv[optind], "m3u" ) ) {
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
		else if ( endsWith( argv[optind], ".sq3" ) ) {
			strncpy( dbname, argv[optind], MAXPATHLEN );
		}
		else {
			strncpy( basedir, argv[optind], MAXPATHLEN );
			if (basedir[strlen(basedir) - 1] == '/')
				basedir[strlen(basedir) - 1] = 0;
		}
	}

	if (0 == strlen( blname) ) {
		strcpy( blname, basedir );
		strcat( blname, "/blacklist.txt" );
	}
	else {
		if( blname[0] != '/' ) {
			strcpy( line, basedir );
			strcat( line, "/" );
			strcat( line, blname );
			strcpy( blname, line );
		}
		loadBlacklist( blname );
	}

	if (0 == strlen( wlname) ) {
		strcpy( wlname, basedir );
		strcat( wlname, "/favourites.txt" );
	}
	else {
		if( wlname[0] != '/' ) {
			strcpy( line, basedir );
			strcat( line, "/" );
			strcat( line, wlname );
			strcpy( wlname, line );
		}
		// loadWhitelist( wlname );
	}

	if( NULL == root ) root = recurse(basedir, root);

	if (NULL != root) {
		if(!stream){
			if (mix)
				root = shuffleTitles(root );
			else
				root = rewindTitles(root );

			if(0 != loadWhitelist(wlname)) {
				checkWhitelist(root);
			}
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
			// dup2( STDERR_FILENO, p_status[0] );

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
			drawframe(station, NULL, status, stream );

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
								if( 0 == strlen(blname) ) {
									strcpy( blname, basedir );
									strcat( blname, "/blacklist.txt" );
								}
								addToList( blname, current->name );
								current=removeTitle( current );
								if( NULL != current->prev ) {
									current=current->prev;
								}
								order=1;
								write( p_command[1], "STOP\n", 6 );
							break;
							case 'f':
								// sprintf( tbuf, "%s/%s", current->path, current->name );
								if(0 == current->rating) {
									addToList( wlname, current->name );
									current->rating=1;
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
								strip( station, line+13, LINE_BUFLEN );
							}
							if( NULL != ( b = strstr( line, "StreamTitle") ) ) {
								b = b + 13;
								*strchr(b, '\'') = '\0';
								if( strlen(current->display) != 0 ) {
									insertTitle( current, current->display );
								}
								strip(current->display, b, LINE_BUFLEN );
							}
						}
						// standard mpg123 info
						else {
							if (NULL != (b = strstr(line, "title:"))) {
								strip(tbuf, b + 6, LINE_BUFLEN);
								// do not redraw yet, wait for the artist
							}
							// line starts with 'Artist:' this means we had a 'Title:' line before
							else if (NULL != (b = strstr(line, "artist:"))) {
								strip(current->display, b + 7, LINE_BUFLEN);
								strcat(current->display, " - ");
								strip(current->display + strlen(current->display), tbuf,
										LINE_BUFLEN - strlen(current->display));
							}
							// Album
							else if (NULL != (b = strstr(line, "album:"))) {
								strip(station, b + 6, LINE_BUFLEN);
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
						redraw=1;
						break;
					case 'P': // Player status
						cmd = atoi(&line[3]);
						switch (cmd) {
						case 0:
							next = skipTitles( current, order, repeat, mix );
							order=1;
							if ( next == current ) {
								strcpy( status, "STOP" );
							}
							else {
								current=next;
								sendplay(p_command[1], current);
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
							drawframe( station, current, status, stream );
							sleep(1);
						}
						redraw=1;
						break;
					case 'E':
						sprintf( status, "ERROR: %s", line);
						drawframe( station, current, status, stream );
						sleep(1);
						break;
					default:
						sprintf( status, "MPG123 : %s", line);
						drawframe( station, current, status, stream );
						sleep(1);
						break;
					} // case()
				} // fgets() > 0
				if( redraw ) drawframe(station, current, status, stream );
			} // while(running)
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
