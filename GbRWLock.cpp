#include "GbRWLock.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include "Sanity.h"

#ifdef DEBUG_RWLOCKS

struct gbrwlock_counter_t {
	int m_readLockCount;
	int m_writeLockCount;
};

extern "C" {
static void gbrwlock_counter_destroy(void *key) {
	gbrwlock_counter_t *gbrwlock_counter = static_cast<gbrwlock_counter_t*>(key);
	free(gbrwlock_counter);
}
}

gbrwlock_counter_t* GbRWLock::getCounter() {
	gbrwlock_counter_t *gbrwlock_counter = static_cast<gbrwlock_counter_t*>(pthread_getspecific(m_counter_key));
	if (!gbrwlock_counter) {
		gbrwlock_counter = static_cast<gbrwlock_counter_t*>(malloc(sizeof(*gbrwlock_counter)));
		gbrwlock_counter->m_readLockCount = 0;
		gbrwlock_counter->m_writeLockCount = 0;
		pthread_setspecific(m_counter_key, gbrwlock_counter);
	}

	return gbrwlock_counter;
}

#endif

GbRWLock::GbRWLock()
	: m_rwlock(PTHREAD_RWLOCK_WRITER_NONRECURSIVE_INITIALIZER_NP) {
#ifdef DEBUG_RWLOCKS
	if (pthread_key_create(&m_counter_key, gbrwlock_counter_destroy) != 0) {
		gbshutdownResourceError();
	}
#endif
}

GbRWLock::~GbRWLock() {
	int rc = pthread_rwlock_destroy(&m_rwlock);
	assert(rc == 0);
#ifdef DEBUG_RWLOCKS
	pthread_key_delete(m_counter_key);
#endif
}

void GbRWLock::readLock() {
#ifdef DEBUG_RWLOCKS
	gbrwlock_counter_t *gbrwlock_counter = getCounter();
	assert(gbrwlock_counter->m_readLockCount == 0);
	++(gbrwlock_counter->m_readLockCount);
#endif
	int rc = pthread_rwlock_rdlock(&m_rwlock);
	assert(rc == 0);
}

void GbRWLock::writeLock() {
#ifdef DEBUG_RWLOCKS
	gbrwlock_counter_t *gbrwlock_counter = getCounter();
	assert(gbrwlock_counter->m_writeLockCount == 0);
	++(gbrwlock_counter->m_writeLockCount);
#endif
	int rc = pthread_rwlock_wrlock(&m_rwlock);
	assert(rc == 0);
}

void GbRWLock::readUnlock() {
#ifdef DEBUG_RWLOCKS
	gbrwlock_counter_t *gbrwlock_counter = getCounter();
	assert(gbrwlock_counter->m_readLockCount == 1);
	--(gbrwlock_counter->m_readLockCount);
#endif
	int rc = pthread_rwlock_unlock(&m_rwlock);
	assert(rc == 0);
}

void GbRWLock::writeUnlock() {
#ifdef DEBUG_RWLOCKS
	gbrwlock_counter_t *gbrwlock_counter = getCounter();
	assert(gbrwlock_counter->m_writeLockCount == 1);
	--(gbrwlock_counter->m_writeLockCount);
#endif
	int rc = pthread_rwlock_unlock(&m_rwlock);
	assert(rc == 0);
}

void GbRWLock::verify_is_locked() {
#ifdef DEBUG_RWLOCKS
	gbrwlock_counter_t *gbrwlock_counter = getCounter();
	assert(gbrwlock_counter->m_readLockCount == 1 || gbrwlock_counter->m_writeLockCount == 1);
#endif
}

void GbRWLock::verify_is_read_locked() {
#ifdef DEBUG_RWLOCKS
	gbrwlock_counter_t *gbrwlock_counter = getCounter();
	assert(gbrwlock_counter->m_readLockCount == 1);
#endif
}

void GbRWLock::verify_is_write_locked() {
#ifdef DEBUG_RWLOCKS
	gbrwlock_counter_t *gbrwlock_counter = getCounter();
	assert(gbrwlock_counter->m_writeLockCount == 1);
#endif
}