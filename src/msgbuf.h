#ifndef __MSGBUF_H__
#define __MSGBUF_H__ 1
#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>

#define MSGNUM 256

typedef struct {
	char *msg;
	int32_t cid;
} clmessage;

/*
 * Message ringbuffer structure
 */
typedef struct {
	clmessage *msg[MSGNUM];		/* the message buffer */
	int32_t current;			/* index if the first unhandles message line */
	int32_t lines;				/* how many message lines are in use */
	uint64_t count;				/* the number of the last message */
	int32_t unread;				/* the number of never read messages */
	pthread_mutex_t *msgLock;	/* mutex to control access to the messages */
	bool shutdown;
} msgbuf_t;

/**
 * helperfunction to implement message ringbuffer
 */
msgbuf_t *msgBuffInit();
uint64_t msgBuffAdd(msgbuf_t * msgbuf, char *line);
uint64_t msgBuffAddCid(msgbuf_t * msgbuf, char *line, uint32_t cid);
const clmessage *msgBuffPeek(msgbuf_t * msgbuf, uint64_t msgno);
char *msgBuffAll(msgbuf_t * msgbuf);
void msgBuffDiscard(msgbuf_t * msgbuf);
uint64_t msgBufGetLastRead(msgbuf_t * msgbuf);
#endif
