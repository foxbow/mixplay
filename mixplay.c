/**
 * all purpose music player with database support and ncurses interface
 * also able to handle ICY streams and playlists
 *
 * supports per session favourites that may be played twice as often as normal songs
 * supports per session DoNotPlay lists to filter out undesired titles
 *
 * main use is to replace the squeezebox(tm) set up and still offer
 * dynamic mix of titles without the overhead of other players like Banshee
 * or even iTunes
 */

#include "utils.h"
#include "musicmgr.h"
#include "database.h"
#include "mpgutils.h"
#define FAIL endwin(); fail
#include "ncbox.h"

#include <ncurses.h>
#include <stdio.h>
#include <getopt.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef VERSION
#define VERSION "dev"
#endif

/**
 * print out CLI usage
 * ACd:Df:hl:mp:Pq:QrR:s:SvTFVX
 */
static void usage( char *progname ) {
    printf( "%s-%s - console frontend to mpg123\n", progname, VERSION );
    printf( "Usage: %s [-u <user>] [-s <key>|-S] [-p <file>] [-m] [-r] "
            "[-v] [-V] [-h] [-C] [-A] [-D] [-T] [-F] [-X] "
            "[-l <skip>] [path|URL]\n", progname );
    printf( "-u <user>  : user for DNP and favourites [mixplay]\n" );
    printf( "-s <term>  : add search term (can be used multiple times)\n" );
    printf( "             [<talgp><=*>]term\n" );
    printf( "-S         : interactive search\n" );
    printf( "-p <file>  : use file as fuzzy playlist (party mode - resets range to Path!)\n" );
    printf( "-P         : like -p but uses the default favourites\n" );
    printf( "-q <dir>   : copy favourites to target <dir> (USB stick, MP3 player..)\n" );
    printf( "-l <skip>  : titles skipped more than <skip> times will be marked DNP [2]\n" );
    printf( "-m         : toggle shuffle (mix) mode\n" );
    printf( "-r         : disable repeat mode on playlist\n" );
    printf( "-v         : increase verbosity (just for debugging)\n" );
    printf( "-V         : print version*\n" );
    printf( "-h         : print this help*\n" );
    printf( "-C         : clear database and add titles anew *\n" );
    printf( "-A         : add new titles to the database *\n" );
    printf( "-D         : delete removed titles from the database *\n" );
    printf( "-F         : disable crossfading between songs\n" );
    printf( "-X         : print some database statistics*\n" );
    printf( "[path|URL] : path to the music files [.]\n" );
    printf( " * these functions will not start the player\n" );
    exit( 0 );
}

/**
 * make mpeg123 play the given title
 */
static void sendplay( int fd, struct entry_t *song ) {
    char line[MAXPATHLEN]="load ";
    strncat( line, song->path, MAXPATHLEN );
    strncat( line, "\n", MAXPATHLEN );

    if ( write( fd, line, MAXPATHLEN ) < strlen( line ) ) {
        FAIL( F_FAIL, "Could not write\n%s", line );
    }
}

#define CP_BUFFSIZE 4096

static void copyTitle( struct entry_t *title, const char* target, const unsigned int index ) {
    FILE *in, *out;
    char filename[MAXPATHLEN];
    unsigned char *buffer;
    int size;

    buffer=falloc( CP_BUFFSIZE, sizeof( char ) );

    snprintf( filename, MAXPATHLEN, "%strack%03i.mp3", target, index );

    printver( 2, "Copy %s to %s\n", title->display, filename );

    in=fopen( title->path, "rb" );

    if( NULL == in ) {
        FAIL( errno, "Couldn't open %s for reading", title->path );
    }

    out=fopen( filename, "wb" );

    if( NULL == out ) {
        FAIL( errno, "Couldn't open %s for writing", filename );
    }

    size = fread( buffer, sizeof( unsigned char ), CP_BUFFSIZE, in );

    while( 0 != size ) {
        activity( "Copying file %03i", index );

        if( 0 == fwrite( buffer, sizeof( unsigned char ), size, out ) ) {
            FAIL( errno, "Target is full!" );
        }

        size = fread( buffer, sizeof( unsigned char ), CP_BUFFSIZE, in );
    }

    fclose( in );
    fclose( out );
    free( buffer );
}
/*
 *
 */
int main( int argc, char **argv ) {
    /**
     * CLI interface
     */
    struct entry_t *root = NULL;
    struct entry_t *current = NULL;
    struct entry_t *next = NULL;

    struct marklist_t *dnplist=NULL;
    struct marklist_t *favourites=NULL;
    struct marklist_t *searchlist=NULL;

    // pipes to communicate with mpg123
    int p_status[2][2];
    int p_command[2][2];

    char line[MAXPATHLEN];
    char status[MAXPATHLEN] = "INIT";
    char tbuf[MAXPATHLEN];
    char basedir[MAXPATHLEN];
    char confdir[MAXPATHLEN];
    char target[MAXPATHLEN] = "";
    char dbname[MAXPATHLEN]  = "mixplay.db";
    char dnpname[MAXPATHLEN] = "mixplay.dnp";
    char favname[MAXPATHLEN] = "mixplay.fav";
    char config[MAXPATHLEN]  = "mixplay.cnf";
    int key;
    char c;
    char *b;
    int mix = -1;
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
    int repeat=1;
    int scan=0;
    int fade=3;
    int fdset=0;
    int invol=100;
    int outvol=100;
    int intime=0;
    int dump=0;
    int hascfg=0;
    unsigned int skiplevel=2;

    FILE *fp=NULL;

    muteVerbosity();

    memset( basedir, 0, MAXPATHLEN );

    // load default configuration
    sprintf( confdir, "%s/.mixplay", getenv( "HOME" ) );
    abspath( config, confdir, MAXPATHLEN );
    fp=fopen( config, "r" );

    if( NULL != fp ) {
        do {
            i=0;
            fgets( line, MAXPATHLEN, fp );

            if( strlen( line ) > 2 ) {
                switch( line[0] ) {
                case 'd':
                    strip( dbname, line+1, MAXPATHLEN );
                    break;

                case 's':
                    strip( basedir, line+1, MAXPATHLEN );
                    break;

                case '#':
                    break;

                default:
                    FAIL( F_FAIL, "Config error: %s", line );
                    break;
                }
            }
        }
        while( !feof( fp ) );

        hascfg=1;
    }

    // if no basedir has been set, use the current directory as default
    if( 0 == strlen( basedir ) && ( NULL == getcwd( basedir, MAXPATHLEN ) ) ) {
        FAIL( errno, "Could not get current directory!" );
    }

    // parse command line options
    while ( ( c = getopt( argc, argv, "ACDhl:mp:Pq:Qrs:Su:vFVX" ) ) != -1 ) {

        switch ( c ) {
        case 'v': // pretty useless in normal use
            incVerbosity();
            break;

        case 'm':
            mix = ~mix;
            break;

        case 'r':
            repeat=0;
            break;

        case 'u':
            snprintf( favname, MAXPATHLEN, "%s.fav", optarg );
            snprintf( dnpname, MAXPATHLEN, "%s.dnp", optarg );
            break;

        case 'S':
            printf( "Search: " );
            fflush( stdout );

            if( readline( line, MAXPATHLEN, fileno( stdin ) ) > 1 ) {
                search=1;
                mix=0;
                searchlist=addToList( line, &searchlist );
            }
            else {
                printf( "-S: Ignoring less than three characters! (%s)", optarg );
                sleep( 3 );
                return -1;
            }

            break;

        case 's':
            if( strlen( optarg ) > 2 ) {
                search=1;
                mix=0;
                searchlist=addToList( optarg, &searchlist );
            }
            else {
                printf( "-s: Ignoring less than three characters! (%s)", optarg );
                sleep( 3 );
                return -1;
            }

            break;

        case 'p':
            search=1;
            searchlist=loadList( optarg );
            break;

        case 'q':
            if( !isDir( optarg ) ) {
                FAIL( F_FAIL, "%s is not a directory", optarg );
            }

            strncpy( target, optarg, MAXPATHLEN );
            break;

        case 'P':
            search=1;
            abspath( favname, confdir, MAXPATHLEN );
            searchlist=loadList( favname );
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

        case 'l':
            skiplevel=atoi( optarg );
            break;

        case 'F':
            fade=0;// atoi(optarg);
            break;

        case 'V':
            printf( "mixplay-%s\n", VERSION );
            return 0;
            break;

        case 'h':
            usage( argv[0] );
            break;

        case 'X':
            mix=0;
            dump=1;
            puts( "-- Dumping Database statistics --" );
            setVerbosity( 2 );
            break;

        case 'Q':
            mix=0;
            dump=-1;
            setVerbosity( 2 );
            break;

        default:
            printf( "Unknown option -%c!\n\n", c );
            usage( argv[0] );
            break;
        }
    }

    // parse additional argument and sanitize options
    if ( optind < argc ) {
        usedb=0;

        if( isURL( argv[optind] ) ) {
            mix=0;		// mixing a stream is a bad idea
            repeat=0;	// no repeat
            fade=0;		// fading is really dumb
            stream=1;
            line[0]=0;

            if( endsWith( argv[optind], ".m3u" ) ||
                    endsWith( argv[optind], ".pls" ) ) {
                strcpy( line, "@" );
            }

            strncat( line, argv[optind], MAXPATHLEN );
            root=insertTitle( root, line );
            root->display[0]=0; // hide URL from display
        }
        else if( endsWith( argv[optind], ".mp3" ) ) {
            // play single song...
            fade=0;
            mix=0;
            repeat=0;
            root=insertTitle( root, argv[optind] );
        }
        else if ( endsWith( argv[optind], ".m3u" ) ||
                  endsWith( argv[optind], ".pls" ) ) {
            usedb=0;
            mix=0;

            if( NULL != strrchr( argv[optind], '/' ) ) {
                strcpy( basedir, argv[optind] );
                i=strlen( basedir );

                while( basedir[i] != '/' ) {
                    i--;
                }

                basedir[i]=0;
                chdir( basedir );
            }

            root=loadPlaylist( argv[optind] );
        }
        else if ( endsWith( argv[optind], ".db" ) ) {
            usedb=1;
            strncpy( dbname, argv[optind], MAXPATHLEN );
        }
        else {
            if( !scan ) {
                usedb=0;
            }

            mix=0;
            getcwd( basedir, MAXPATHLEN );

            if( argv[optind][0] != '/' ) {
                snprintf( line, MAXPATHLEN, "%s/%s", basedir, argv[optind] );
                strncpy( basedir, line, MAXPATHLEN );
            }
            else {
                strncpy( basedir, argv[optind], MAXPATHLEN );
            }
        }
    }

    while ( basedir[strlen( basedir )-1] == '/' ) {
        basedir[strlen( basedir )-1] = 0;
    }

    abspath( dbname, confdir, MAXPATHLEN );

    if( hascfg==0 && ( strcmp(  getenv( "HOME" ), basedir ) == 0 ) ) {
        printf( "basedir is set to %s\n", basedir );
        printf( "This is usually a bad idea and a sign that the default\n" );
        printf( "music directory needs to be set.\n" );
        printf( "It will be set up now\n" );

        if( mkdir( confdir, 0700 ) == -1 ) {
            FAIL( errno, "Could not create config dir %s", confdir );
        }

        while( 1 ) {
            printf( "Default music directory:" );
            fflush( stdout );
            memset( basedir, 0, MAXPATHLEN );
            fgets( basedir, MAXPATHLEN, stdin );
            basedir[strlen( basedir )-1]=0; // cut off CR
            abspath( basedir, getenv( "HOME" ), MAXPATHLEN );

            if( isDir( basedir ) ) {
                break;
            }
            else {
                printf( "%s is not a directory!\n", basedir );
            }
        }

        fp=fopen( config, "w" );

        if( NULL != fp ) {
            fputs( "# mixplay configuration\n", fp );
            fputc( 's', fp );
            fputs( basedir, fp );
            fputc( '\n', fp );
            fclose( fp );
            FAIL( F_FAIL, "Done." );
        }
        else {
            FAIL( errno, "Could not open %s", line );
        }
    }

    // scanformusic functionality
    if( scan ) {
        if ( scan & 1 ) {
            dbBackup( dbname );
        }

        if( scan & 2 ) {
            dbAddTitles( dbname, basedir );
        }

        if( scan & 4 ) {
            dbCheckExist( dbname );
        }

        return 0;
    }

    abspath( dnpname, confdir, MAXPATHLEN );

    if( usedb ) {
        dnplist=loadList( dnpname );
    }

    // set default favourites name
    abspath( favname, confdir, MAXPATHLEN );

    if( usedb ) {
        favourites=loadList( favname );
    }

    // load and prepare titles
    if( NULL == root ) {
        if( usedb ) {
            root=dbGetMusic( dbname );
        }
        else {
            root=recurse( basedir, NULL, basedir );

            if( root != NULL ) {
                root=root->dbnext;
                current=root;

                do {
                    activity( "Relinking" );
                    current->plnext=current->dbnext;
                    current->plprev=current->dbprev;
                    current=current->dbnext;
                }
                while( current != root );
            }
        }

        if( usedb && ( skiplevel > 0 ) ) {
            DNPSkip( root, skiplevel );
        }

        if ( usedb ) {
            applyDNPlist( root, dnplist );
            applyFavourites( root, favourites );
        }

        if( search ) {
            root=searchList( root, searchlist );
        }
    }

    // No else as the above calls may return with an empty playlist!
    // prepare playing the titles
    if ( NULL != root ) {

        if( strlen( target ) > 0 ) {
            current=root;

            do {
                if( !( current->flags & MP_FAV ) ) {
                    current->flags |= MP_MARK;
                }

                current=current->dbnext;
            }
            while( current != root );
        }

        if ( mix ) {
            root=shuffleTitles( root );
        }

        if( strlen( target ) > 0 ) {
            unsigned int index=0;
            current=root;

            do {
                copyTitle( current, target, index++ );
                current=current->plnext;
            }
            while( current != root );

            printver( 1, "Copied %i titles to %s\n", index, target );
            return 0;
        }

        if( dump ) { // database statistics
            if( -1 == dump ) {
                dumpTitles( root,0 );
            }

            dumpInfo( root, ~mix );
            return 0;
        }

        // start the player processes
        for( i=0; i <= ( fade?1:0 ); i++ ) {
            // create communication pipes
            pipe( p_status[i] );
            pipe( p_command[i] );

            pid[i] = fork();

            if ( 0 > pid[i] ) {
                FAIL( errno, "could not fork" );
            }

            // child process
            if ( 0 == pid[i] ) {
                if ( dup2( p_command[i][0], STDIN_FILENO ) != STDIN_FILENO ) {
                    FAIL( errno, "Could not dup stdin for player %i", i+1 );
                }

                if ( dup2( p_status[i][1], STDOUT_FILENO ) != STDOUT_FILENO ) {
                    FAIL( errno, "Could not dup stdout for player %i", i+1 );
                }

                // this process needs no pipes
                close( p_command[i][0] );
                close( p_command[i][1] );
                close( p_status[i][0] );
                close( p_status[i][1] );

                // Start mpg123 in Remote mode
                execlp( "mpg123", "mpg123", "-R", "2>/dev/null", NULL );
                // execlp("mpg123", "mpg123", "-R", "--remote-err", NULL); // breaks the reply parsing!
                FAIL( errno, "Could not exec mpg123" );
            }

            close( p_command[i][0] );
            close( p_status[i][1] );
        }

        running=1;

        if( usedb ) {
            db=dbOpen( dbname );
        }

        // Start curses mode
        initscr();
        curs_set( 0 );
        cbreak();
        keypad( stdscr, TRUE );
        noecho();
        drawframe( NULL, status, stream );

        key = fcntl( p_status[0][0], F_GETFL, 0 );
        fcntl( p_status[0][0], F_SETFL, key | O_NONBLOCK );

        if( fade ) {
            key = fcntl( p_status[1][0], F_GETFL, 0 );
            fcntl( p_status[1][0], F_SETFL, key | O_NONBLOCK );
        }

        while ( running ) {
            FD_ZERO( &fds );
            FD_SET( fileno( stdin ), &fds );
            FD_SET( p_status[0][0], &fds );

            if( fade != 0 ) {
                FD_SET( p_status[1][0], &fds );
            }

            to.tv_sec=1;
            to.tv_usec=0; // 1 second
            i=select( FD_SETSIZE, &fds, NULL, NULL, &to );

            if( i>0 ) {
                redraw=1;
            }

            // Interpret key
            if( FD_ISSET( fileno( stdin ), &fds ) ) {
                key=getch();

                if( popUpActive() ) {
                    popDown();
                }
                else {
                    switch( key ) {
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
                        case 'i':
                            if( 0 != current->key ) {
                                popUp( 0, "%s\nGenre: %s\nKey: %04i\nplaycount: %i\nskipcount: %i\nCount: %s - Skip: %s",
                                       current->path, current->genre,
                                       current->key, current->playcount,
                                       current->skipcount,
                                       ONOFF( ~( current->flags )&MP_CNTD ),
                                       ONOFF( ~( current->flags )&MP_SKPD ) );
                            }
                            else {
                                popUp( 2, current->path );
                            }

                            break;

                        case 'S':
                        case 'J':
                            popAsk( "Search: ", line );

                            if( strlen( line ) > 2 ) {
                                next=current;

                                do {
                                    if( checkMatch( next->path, line ) ) {
                                        break;
                                    }

                                    next=next->plnext; // @TODO: include DNPs?
                                }
                                while( current != next );

                                if( next != current ) {
                                    next->flags|=MP_CNTD; // Don't count searched titles.
                                    moveEntry( next, current );
                                    order=1;
                                    write( p_command[fdset][1], "STOP\n", 6 );
                                }
                                else {
                                    popUp( 2, "Nothing found for %s", line );
                                }
                            }
                            else {
                                popUp( 2, "Need at least 3 characters.." );
                            }

                            break;

                        case 'F':
                            fade=fade?0:3;
                            popUp( 1, "Fading is now %s", ONOFF( fade ) );
                            break;

                        case 'R':
                            repeat=repeat?0:1;
                            popUp( 1, "Repeat is now %s", ONOFF( repeat ) );
                            break;

                        case 'I':
                            popUp( 0, "usedb: %s - mix: %s - repeat:%s - fade: %is\n"
                                   "basedir: %s\n"
                                   "dnplist: %s\n"
                                   "favlist: %s\n"
                                   "config:  %s"
                                   , ONOFF( usedb ), ONOFF( mix ), ONOFF( repeat ), fade,
                                   basedir, dnpname, favname, config );
                            break;

                        case KEY_DOWN:
                        case 'n':
                            order=1;

                            if( !( current->flags & ( MP_SKPD | MP_CNTD ) ) ) {
                                current->skipcount++;
                                current->flags |= MP_SKPD;
                            }

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
                            addToFile( dnpname, current->display, "d=" );
                            current=removeByPattern(current,  "d=");
                            order=1;
                            write( p_command[fdset][1], "STOP\n", 6 );
                            break;

                        case 'B':
                            addToFile( dnpname, current->album, "l=" );
                            popUp( 2, "Added %s to the DNP list\n", current->album );
                            current=removeByPattern(current, "l=" );
                            order=1;
                            write( p_command[fdset][1], "STOP\n", 6 );
                            break;

                        case 'f': // toggles the favourite flag on a title
                            if( !( current->flags & MP_FAV ) ) {
                                addToFile( favname, current->display, "d=" );
                                current->flags|=MP_FAV;
                            }

                            break;
                        }
                    }
                }
            }

            // drain inactive player
            if( fade && FD_ISSET( p_status[fdset?0:1][0], &fds ) ) {
                key=readline( line, 512, p_status[fdset?0:1][0] );

                if( ( key > 0 ) && ( outvol > 0 ) && ( line[1] == 'F' ) ) {
                    outvol--;
                    snprintf( line, MAXPATHLEN, "volume %i\n", outvol );
                    write( p_command[fdset?0:1][1], line, strlen( line ) );
                }
            }

            // Interpret mpg123 output and ignore invalid lines
            if( FD_ISSET( p_status[fdset][0], &fds ) &&
                    ( 3 < readline( line, 512, p_status[fdset][0] ) ) ) {
                if( '@' == line[0] ) {
                    switch ( line[1] ) {
                        int cmd=0, rem=0, q=0;

                    case 'R': // startup
                        current = root;
                        sendplay( p_command[fdset][1], current );
                        break;

                    case 'I': // ID3 info

                        // ICY stream info
                        if( NULL != strstr( line, "ICY-" ) ) {
                            if( NULL != strstr( line, "ICY-NAME: " ) ) {
                                strip( current->album, line+13, NAMELEN );
                            }

                            if( NULL != ( b = strstr( line, "StreamTitle" ) ) ) {
                                b = b + 13;
                                *strchr( b, '\'' ) = '\0';

                                if( strlen( current->display ) != 0 ) {
                                    strcpy( tbuf, current->display );
                                    next=insertTitle( current, tbuf );
                                    // fix genpathname() from insertTitle
                                    strip( next->display, tbuf, MAXPATHLEN );
                                }

                                strip( current->display, b, MAXPATHLEN );
                            }
                        }
                        // standard mpg123 info
                        else if ( strstr( line, "ID3" ) != NULL ) {
                            if( !usedb ) {
                                if ( NULL != ( b = strstr( line, "title:" ) ) ) {
                                    strip( current->title, b + 6, NAMELEN );
                                    snprintf( current->display, MAXPATHLEN, "%s - %s",
                                              current->artist, current->title );
                                }
                                // line starts with 'Artist:' this means we had a 'Title:' line before
                                else if ( NULL != ( b = strstr( line, "artist:" ) ) ) {
                                    strip( current->artist, b + 7, NAMELEN );
                                    snprintf( current->display, MAXPATHLEN, "%s - %s",
                                              current->artist, current->title );
                                }
                                // Album
                                else if ( NULL != ( b = strstr( line, "album:" ) ) ) {
                                    strip( current->album, b + 6, NAMELEN );
                                }
                                else if( NULL != ( b = strstr( line, "genre:" ) ) ) {
                                    strip( current->genre, b+6, NAMELEN );
                                }
                            }
                        }
                        else {
                            strip( status, &line[3], 10 );
                            drawframe( current, status, stream );
                            sleep( 2 );
                        }

                        redraw=1;
                        break;

                    case 'T': // TAG reply
                        FAIL( F_FAIL, "Got TAG reply!" );
                        break;
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
                        rem=atoi( b );
                        *b=0;
                        b=strrchr( line, ' ' );
                        intime=atoi( b );

                        // stream play
                        if( stream ) {
                            if( intime/60 < 60 ) {
                                sprintf( status, "%i:%02i PLAYING", intime/60, intime%60 );
                            }
                            else {
                                sprintf( status, "%i:%02i:%02i PLAYING", intime/3600, ( intime%3600 )/60, intime%60 );
                            }
                        }
                        // file play
                        else {
                            q=( 30*intime )/( rem+intime );
                            memset( tbuf, 0, MAXPATHLEN );

                            for( i=0; i<30; i++ ) {
                                if( i < q ) {
                                    tbuf[i]='=';
                                }
                                else if( i == q ) {
                                    tbuf[i]='>';
                                }
                                else {
                                    tbuf[i]=' ';
                                }
                            }

                            sprintf( status, "%i:%02i [%s] %i:%02i", intime/60, intime%60, tbuf, rem/60, rem%60 );

                            if( ( fade != 0 ) && ( rem <= fade ) ) {
                                // should the playcount be increased?
                                // mix     - playcount relevant
                                // usedb   - playcount is persistent
                                // search  - partymode
                                // !MP_CNTD - title has not been counted yet
                                if( mix && usedb && !search && !( current->flags & MP_CNTD ) ) {
                                    current->flags |= MP_CNTD; // make sure a title is only counted once per session
                                    current->playcount++;

                                    if( current->skipcount > 0 ) {
                                        current->skipcount--;
                                    }

                                    dbPutTitle( db, current );
                                }

                                next=current->plnext;

                                if( ( next == current )
                                        || ( ( !repeat ) && ( next == root ) ) ) {
                                    strcpy( status, "STOP" );
                                }
                                else {
                                    current=next;
                                    // swap player
                                    fdset=fdset?0:1;
                                    invol=0;
                                    outvol=100;
                                    write( p_command[fdset][1], "volume 0\n", 9 );
                                    sendplay( p_command[fdset][1], current );
                                }
                            }

                            if( invol < 100 ) {
                                invol++;
                                snprintf( line, MAXPATHLEN, "volume %i\n", invol );
                                write( p_command[fdset][1], line, strlen( line ) );
                            }

                        }

                        redraw=1;
                        break;

                    case 'P': // Player status
                        cmd = atoi( &line[3] );

                        switch ( cmd ) {
                        case 0:

                            // should the playcount be increased?
                            // search  - partymode
                            // mix     - playcount relevant
                            // intime  - has been played for more than 2 secs
                            // usedb   - playcount is persistent
                            // !MP_CNTD - title has not been counted yet
                            if ( !search && mix && ( intime > 2 ) && usedb && !( current->flags & MP_CNTD ) ) {
                                current->flags |= MP_CNTD;
                                current->playcount++;

                                if( current->skipcount> 0 ) {
                                    current->skipcount--;
                                }

                                dbPutTitle( db, current );
                            }

                            next = skipTitles( current, order, 0 );

                            if ( ( next == current ) ||
                                    ( !repeat &&
                                      ( ( ( order == 1 ) && ( next == root ) ) ||
                                        ( ( order == -1 ) && ( next == root->plprev ) ) ) ) ) {
                                strcpy( status, "STOP" );
                            }
                            else {
                                if( ( order==1 ) && ( next == root ) ) {
                                    newCount( root );
                                }

                                current=next;
                                sendplay( p_command[fdset][1], current );
                            }

                            order=1;
                            break;

                        case 1:
                            strcpy( status, "PAUSE" );
                            break;

                        case 2:
                            strcpy( status, "PLAYING" );
                            break;

                        default:
                            popUp( 0, "Unknown status!\n %i", cmd );
                            break;
                        }

                        redraw=1;
                        break;

                    case 'V': // volume reply
                        redraw=0;
                        break;

                    case 'E':
                        FAIL( F_FAIL, "ERROR: %s\nIndex: %i\nName: %s\nPath: %s", line,
                              current->key,
                              current->display,
                              current->path );
                        break;

                    default:
                        popUp( 0, "Warning!\n%s", line );
                        break;
                    } // case line[1]
                } // if line starts with '@'

                // Ignore other mpg123 output
            } // fgets() > 0

            if( redraw ) {
                drawframe( current, status, stream );
            }
        } // while(running)

        if( usedb ) {
            dbClose( db );
        }

        kill( pid[0], SIGTERM );

        if( fade ) {
            kill( pid[1], SIGTERM );
        }

        endwin();
    } // root==NULL
    else {
        if( usedb ) {
            FAIL( F_WARN, "No matching music found in %s", dbname );
        }
        else {
            FAIL( F_WARN, "No matching music found at %s", basedir );
        }
    }

    return 0;
}
