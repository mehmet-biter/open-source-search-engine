#ifndef GB_SCOPEDWRITELOCK_H
#define GB_SCOPEDWRITELOCK_H

#include "GbRWLock.h"
#include <assert.h>

class ScopedWriteLock {
public:
	ScopedWriteLock(GbRWLock &rwlock)
		: m_rwlock(rwlock)
		, m_locked(true) {
		m_rwlock.writeLock();
	}

	~ScopedWriteLock() {
		if (m_locked) {
			m_rwlock.writeUnlock();
		}
	}
	void unlock() {
		assert(m_locked);
		m_rwlock.writeUnlock();
		m_locked = false;
	}

private:
	ScopedWriteLock(const ScopedWriteLock&);
	ScopedWriteLock& operator=(const ScopedWriteLock&);

	GbRWLock &m_rwlock;
	bool m_locked;
};

#endif //GB_SCOPEDWRITELOCK_H
