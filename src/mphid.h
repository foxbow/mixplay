#ifndef __MPHID_H__
#define __MPHID_H__ 1
#include <pthread.h>
#include "config.h"

pthread_t startHID(int32_t fd);
int32_t initHID();
mpcmd_t hidCMD(int32_t c);
void hidPrintline(const char *text, ...)
	__attribute__ ((__format__(__printf__, 1, 2)));
#endif
