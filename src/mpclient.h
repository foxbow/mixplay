#ifndef __MPCLIENT_H__
#define __MPCLIENT_H__ 1
#include "config.h"

int getConnection();
int sendCMD(int fd, mpcmd_t cmd);
int getCurrentTitle( char *title, int tlen );
#endif
