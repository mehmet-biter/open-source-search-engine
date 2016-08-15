#include "GbMutex.h"
#include <errno.h>
#include <assert.h>

//The initialization and raw copying of pthread_mutex_t(init-value) is not
//portable, but works fine on linux, solaris, hp-ux, ...

#ifndef DEBUG_MUTEXES

pthread_mutex_t const GbMutex::mtx_init = PTHREAD_MUTEX_INITIALIZER;

#else

pthread_mutex_t const GbMutex::mtx_init = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP;

GbMutex::~GbMutex() {
	int rc = pthread_mutex_destroy(&mtx);
	assert(rc==0);
}


void GbMutex::lock() {
	int rc = pthread_mutex_lock(&mtx);
	assert(rc==0);
}

void GbMutex::unlock() {
	int rc = pthread_mutex_unlock(&mtx);
	assert(rc==0);
}

void GbMutex::verify_is_locked() {
	int rc = pthread_mutex_lock(&mtx);
	assert(rc==EDEADLK);
}

#endif
