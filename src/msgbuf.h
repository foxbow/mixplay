#ifndef __MSGBUF_H__
#define __MSGBUF_H__ 1
#include <pthread.h>

#define MSGNUM 20

/*
 * Message ringbuffer structure
 */
typedef struct {
	char *msg[MSGNUM];			/* the message buffer */
	int  current;				/* index if the first unhandles message line */
	int  lines;					/* how many message lines are in use */
	unsigned long count;		/* the number of the last read message */
	pthread_mutex_t *msgLock;	/* mutex to control access to the messages */
}  msgbuf_t;

/**
 * helperfunction to implement message ringbuffer
 */
msgbuf_t *msgBuffInit();
unsigned long msgBuffAdd( msgbuf_t *msgbuf, char *line );
char *msgBuffGet( msgbuf_t *msgbuf );
const char *msgBuffPeek( msgbuf_t *msgbuf, unsigned long msgno );
char *msgBuffAll(  msgbuf_t *msgbuf );
void  msgBuffClear( msgbuf_t *msgbuf );
void  msgBuffDiscard( msgbuf_t *msgbuf );

#endif