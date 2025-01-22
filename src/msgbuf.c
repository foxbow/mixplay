#include "msgbuf.h"
#include "utils.h"
#include "mpcomm.h"

/*
 * initialize a message ringbuffer
 */
msgbuf_t *msgBuffInit() {
	msgbuf_t *msgBuf = (msgbuf_t *) falloc(1, sizeof (msgbuf_t));

	msgBuf->msgLock = (pthread_mutex_t *) falloc(1, sizeof (pthread_mutex_t));
	msgBuf->lines = 0;
	msgBuf->current = 0;
	msgBuf->count = 0;
	msgBuf->unread = 0;
	pthread_mutex_init(msgBuf->msgLock, NULL);
	msgBuf->shutdown = false;
	return msgBuf;
}

/**
 * helperfunction to implement message ringbuffer
 * adds message 'line' to the buffer.
 * returns the current message number
 */
uint64_t msgBuffAdd(msgbuf_t * msgbuf, char *line) {
	clmessage *msg = (clmessage *) falloc(1, sizeof (clmessage));

	msg->msg = strdup(line);
	msg->cid = getCurClient();
	if (msgbuf->shutdown)
		return 0;
	pthread_mutex_lock(msgbuf->msgLock);
	/* overflow? */
	if (msgbuf->lines == MSGNUM) {
		/* discard oldest (current) message */
		free(msgbuf->msg[msgbuf->current]->msg);
		free(msgbuf->msg[msgbuf->current]);
		/* replace with new message */
		msgbuf->msg[msgbuf->current] = msg;
		/* bump current message to the next oldest */
		msgbuf->current = (msgbuf->current + 1) % MSGNUM;
	}
	else {
		/* current+lines points to the next free buffer */
		msgbuf->msg[(msgbuf->current + msgbuf->lines) % MSGNUM] = msg;
		msgbuf->lines++;
	}
	msgbuf->count++;
	if (msgbuf->unread < MSGNUM) {
		msgbuf->unread++;
	}
	pthread_mutex_unlock(msgbuf->msgLock);
	return msgbuf->count;
}

/**
 * helperfunction to implement message ringbuffer
 * returns the current message and removes it from the buffer
 * Return pointer must be free'd after use!
 */
static clmessage *msgBuffGet(msgbuf_t * msgbuf) {
	clmessage *retval = NULL;

	if (msgbuf->lines > 0) {
		retval = msgbuf->msg[msgbuf->current];
		msgbuf->msg[msgbuf->current] = NULL;
		msgbuf->current = (msgbuf->current + 1) % MSGNUM;
		msgbuf->lines--;
		msgbuf->unread--;
	}
	return retval;
}

/**
 * helperfunction to implement message ringbuffer
 * returns the current message and keeps it in the buffer
 * Return pointer MUST NOT be free'd after use!
 * Caveat: Returns "" if no messages are available
 */
const clmessage *msgBuffPeek(msgbuf_t * msgbuf, uint64_t msgno) {
	const clmessage *retval = NULL;
	int32_t pos;

	if (msgbuf->shutdown)
		return NULL;
	pthread_mutex_lock(msgbuf->msgLock);
	if (msgno < msgbuf->count) {
		/* Avoid Underflows! */
		if (msgno >= msgbuf->count - msgbuf->lines) {
			pos = msgbuf->current + msgbuf->lines;	/* the latest entry */
			pos = pos - (msgbuf->count - msgno);	/* get the proper offset */
			retval = msgbuf->msg[pos % MSGNUM];
			if (msgbuf->unread > (long) (msgbuf->count - (msgno + 1))) {
				msgbuf->unread = msgbuf->count - (msgno + 1);
			}
		}
	}
	pthread_mutex_unlock(msgbuf->msgLock);

	return retval;
}

/**
 * returns all lines in the buffer as a single string
 * Does not empty the buffer
 * Return pointer SHOULD be free'd after use!
 * Caveat: Returns NULL if no messages are available
 */
char *msgBuffAll(msgbuf_t * msgbuf) {
	int32_t lineno;
	char *buff;
	size_t len = 256;

	buff = (char *) falloc(len, 1);
	buff[0] = 0;

	if (msgbuf->shutdown)
		return buff;
	pthread_mutex_lock(msgbuf->msgLock);
	for (int i = 0; i < msgbuf->lines; i++) {
		lineno = (i + msgbuf->current) % MSGNUM;
		while (strlen(buff) + strlen(msgbuf->msg[lineno]->msg) >= len) {
			len = len + 256;
			buff = (char *) frealloc(buff, len);
		}
		strcat(buff, msgbuf->msg[lineno]->msg);
		strcat(buff, "\n");
	}
	msgbuf->unread = 0;
	pthread_mutex_unlock(msgbuf->msgLock);

	return buff;
}

/* returns the number of the last unread message */
uint64_t msgBufGetLastRead(msgbuf_t * msgbuf) {
	return msgbuf->count - msgbuf->unread;
}

/**
 * empties the message buffer
 */
static void msgBuffClear(msgbuf_t * msgbuf) {
	clmessage *line;

	while ((line = msgBuffGet(msgbuf)) != NULL) {
		free(line->msg);
		free(line);
	}
	msgbuf->lines = 0;
	msgbuf->current = 0;
	msgbuf->count = 0;
	msgbuf->unread = 0;
}

/*
 * Discards the message buffer and all contents
 */
void msgBuffDiscard(msgbuf_t * msgbuf) {
	msgbuf->shutdown = true;
	/* make sure that the mutex is unlocked */
	pthread_mutex_trylock(msgbuf->msgLock);
	pthread_mutex_unlock(msgbuf->msgLock);
	msgBuffClear(msgbuf);
	free(msgbuf->msgLock);
	free(msgbuf);
}
