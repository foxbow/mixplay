#ifndef __MPHID_H__
#define __MPHID_H__ 1
#include "config.h"

mpcmd_t hidCMD(int32_t c);
void hidPrintline(const char *text, ...)
	__attribute__ ((__format__(__printf__, 1, 2)));
#endif
