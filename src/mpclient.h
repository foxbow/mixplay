#ifndef __MPCLIENT_H__
#define __MPCLIENT_H__ 1
#include "config.h"
#include "json.h"
/* so that MPCOMM_ macros are available */
#include "mpcomm.h"

typedef struct {
	int32_t fd;
	int32_t clientid;
} clientInfo;

int32_t setMPPort(int32_t port);
int32_t setMPHost(const char *host);
const char *getMPHost(void);
clientInfo *getConnection(int32_t keep);

int32_t sendCMD(clientInfo * usefd, mpcmd_t cmd);
int32_t getCurrentTitle(char *title, uint32_t tlen);
jsonObject *getStatus(clientInfo * usefd, int32_t flags);
int32_t jsonGetTitle(jsonObject * jo, const char *key, mptitle_t * title);

#endif
