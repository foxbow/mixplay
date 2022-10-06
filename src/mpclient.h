#ifndef __MPCLIENT_H__
#define __MPCLIENT_H__ 1
#include "config.h"
#include "json.h"
/* so that MPCOMM_ macros are available */
#include "mpcomm.h"
#include <syslog.h>

int32_t setMPPort(int32_t port);
int32_t setMPHost(const char *host);
const char *getMPHost(void);
int getConnection(void);

int32_t sendCMD(mpcmd_t cmd, const char *arg);
int32_t getCurrentTitle(char *title, uint32_t tlen);
jsonObject *getStatus(int32_t flags);
int32_t jsonGetTitle(jsonObject * jo, const char *key, mptitle_t * title);

#endif
