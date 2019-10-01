#ifndef __MPHID_H__
#define __MPHID_H__ 1
#include <pthread.h>

int initHID( void );
pthread_t startHID( int fd );
#endif
