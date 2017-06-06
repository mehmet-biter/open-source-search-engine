#ifndef GB_SCOPEDREADLOCK_H
#define GB_SCOPEDREADLOCK_H

#include "GbRWLock.h"
#include <assert.h>

class ScopedReadLock {
public:
	ScopedReadLock(GbRWLock &rwlock)
		: m_rwlock(rwlock)
		, m_locked(true) {
		m_rwlock.readLock();
	}

	~ScopedReadLock() {
		if (m_locked) {
			m_rwlock.readUnlock();
		}
	}
	void unlock() {
		assert(m_locked);
		m_rwlock.readUnlock();
		m_locked = false;
	}

private:
	ScopedReadLock(const ScopedReadLock&);
	ScopedReadLock& operator=(const ScopedReadLock&);

	GbRWLock &m_rwlock;
	bool m_locked;
};

#endif //GB_SCOPEDREADLOCK_H
