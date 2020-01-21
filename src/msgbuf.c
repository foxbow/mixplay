#include "msgbuf.h"
#include "utils.h"

/*
 * initialize a message ringbuffer
 */
msgbuf_t *msgBuffInit() {
	msgbuf_t *msgBuf=(msgbuf_t *)falloc( 1, sizeof( msgbuf_t ) );
	msgBuf->msgLock=(pthread_mutex_t *)falloc( 1, sizeof( pthread_mutex_t ) );
	msgBuf->lines=0;
	msgBuf->current=0;
	msgBuf->count=0;
	msgBuf->unread=0;
	pthread_mutex_init( msgBuf->msgLock, NULL );
	return msgBuf;
}

/**
 * helperfunction to implement message ringbuffer
 * adds message 'line' to the buffer.
 * returns the current message number
 */
unsigned long msgBuffAdd( msgbuf_t *msgbuf, char *line ) {
	char *myline;
	myline=(char*)falloc( strlen(line)+1, 1 );
	strcpy( myline, line );
	pthread_mutex_lock( msgbuf->msgLock );
	/* overflow? */
	if( msgbuf->lines == MSGNUM ) {
		/* discard oldest (current) message */
		free(msgbuf->msg[msgbuf->current]);
		/* replace with new message */
		msgbuf->msg[msgbuf->current]=myline;
		/* bump current message to the next oldest */
		msgbuf->current=(msgbuf->current+1)%MSGNUM;
	}
	else {
		/* current+lines points to the next free buffer */
		msgbuf->msg[(msgbuf->current+msgbuf->lines)%MSGNUM]=myline;
		msgbuf->lines++;
	}
	msgbuf->count++;
	if( msgbuf->unread < MSGNUM ) {
		msgbuf->unread++;
	}
	pthread_mutex_unlock( msgbuf->msgLock );
	return msgbuf->count;
}

/**
 * helperfunction to implement message ringbuffer
 * returns the current message and removes it from the buffer
 * Return pointer must be free'd after use!
 */
char *msgBuffGet( msgbuf_t *msgbuf ) {
	char *retval = NULL;
	pthread_mutex_lock( msgbuf->msgLock );
	if( msgbuf->lines > 0 ) {
		retval=msgbuf->msg[msgbuf->current];
		msgbuf->msg[msgbuf->current]=NULL;
		msgbuf->current =(msgbuf->current+1)%MSGNUM;
		msgbuf->lines--;
		msgbuf->unread--;
	}
	pthread_mutex_unlock( msgbuf->msgLock );
	return retval;
}

/**
 * helperfunction to implement message ringbuffer
 * returns the current message and keeps it in the buffer
 * Return pointer MUST NOT be free'd after use!
 * Caveat: Returns "" if no messages are available
 */
const char *msgBuffPeek( msgbuf_t *msgbuf, unsigned long msgno ) {
	const char *retval = "";
	int pos;

	pthread_mutex_lock( msgbuf->msgLock );
	if( msgno < msgbuf->count ) {
		/* Avoid Underflows!*/
		if( msgno >= msgbuf->count-msgbuf->lines ) {
			pos=msgbuf->current+msgbuf->lines; /* the latest entry */
			pos=pos-(msgbuf->count-msgno ); /* get the proper offset */
			retval=msgbuf->msg[pos%MSGNUM];
			if( msgbuf->unread < (long)(msgbuf->count - msgno) ) {
				msgbuf->unread=msgbuf->count-msgno;
			}
		}
	}
	pthread_mutex_unlock( msgbuf->msgLock );

	return retval;
}

/**
 * returns all lines in the buffer as a single string
 * Does not empty the buffer
 * Return pointer SHOULD be free'd after use!
 * Caveat: Returns NULL if no messages are available
 */
char *msgBuffAll( msgbuf_t *msgbuf ) {
	int i, lineno;
	char *buff;
	size_t len=256;

	buff=(char*)falloc( len, 1 );
	buff[0]=0;

	pthread_mutex_lock( msgbuf->msgLock );
	for( i=0; i<msgbuf->lines; i++ ) {
		lineno=(i+msgbuf->current)%MSGNUM;
		while( strlen(buff)+strlen(msgbuf->msg[lineno]) >= len ) {
			len=len+256;
			buff=(char*)frealloc( buff, len );
		}
		strcat( buff, msgbuf->msg[lineno] );
		strcat( buff, "\n" );
	}
	msgbuf->unread=0;
	pthread_mutex_unlock( msgbuf->msgLock );

	return buff;
}

/* returns the number of the last unread message */
unsigned long msgBufGetLastRead( msgbuf_t *msgbuf ) {
	return msgbuf->count - msgbuf->unread;
}

/**
 * empties the message buffer
 */
void msgBuffClear( msgbuf_t *msgbuf ) {
	char *line;
	while( ( line=msgBuffGet( msgbuf ) ) != NULL ) {
		free( line );
	}
	msgbuf->lines=0;
	msgbuf->current=0;
	msgbuf->count=0;
	msgbuf->unread=0;
}

/*
 * Discards the message buffer and all contents
 */
void msgBuffDiscard( msgbuf_t *msgbuf ) {
	msgBuffClear( msgbuf );
	free( msgbuf->msgLock );
	free( msgbuf );
}
