#ifndef __MPCLIENT_H__
#define __MPCLIENT_H__ 1
#include "config.h"
#include "json.h"
/* so that MPCOMM_ macros are available */
#include "mpcomm.h"

int getConnection();
int sendCMD(int usefd, mpcmd_t cmd);
int getCurrentTitle(char *title, unsigned tlen);
jsonObject *getStatus(int usefd, int flags);

#endif
