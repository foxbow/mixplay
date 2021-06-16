#ifndef __MPCLIENT_H__
#define __MPCLIENT_H__ 1
#include "config.h"
#include "json.h"
/* so that MPCOMM_ macros are available */
#include "mpcomm.h"

typedef struct {
	int fd;
	int clientid;
} clientInfo;

int setMPPort(int port);
int setMPHost(const char *host);
clientInfo *getConnection(int keep);

int sendCMD(clientInfo * usefd, mpcmd_t cmd);
int getCurrentTitle(char *title, unsigned tlen);
jsonObject *getStatus(clientInfo * usefd, int flags);
int jsonGetTitle(jsonObject * jo, const char *key, mptitle_t * title);

#endif
