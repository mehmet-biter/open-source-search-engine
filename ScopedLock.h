#ifndef SCOPED_LOCK_H_
#define SCOPED_LOCK_H_

#include <pthread.h>
#include <assert.h>

//small scoped lock on mutexes
class ScopedLock {
	pthread_mutex_t &mtx;
	bool locked;
	ScopedLock(const ScopedLock&);
	ScopedLock& operator=(const ScopedLock&);
public:
	ScopedLock(pthread_mutex_t &mtx_)
	  : mtx(mtx_), locked(true)
	{
		int rc = pthread_mutex_lock(&mtx);
		assert(rc==0);
	}
	~ScopedLock() {
		if(locked) {
			int rc = pthread_mutex_unlock(&mtx);
			assert(rc==0);
		}
	}
	void unlock() {
		assert(locked);
		int rc = pthread_mutex_unlock(&mtx);
		assert(rc==0);
		locked = false;
	}
};

#endif
