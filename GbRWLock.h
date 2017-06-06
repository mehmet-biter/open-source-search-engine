#ifndef GB_GBRWLOCK_H
#define GB_GBRWLOCK_H

#include <pthread.h>

class GbRWLock {
public:
	GbRWLock();
	~GbRWLock();

	void readLock();
	void writeLock();

	void readUnlock();
	void writeUnlock();

	void verify_is_locked();
	void verify_is_read_locked();
	void verify_is_write_locked();

private:
	GbRWLock(const GbRWLock&);
	GbRWLock& operator=(const GbRWLock&);

	pthread_rwlock_t m_rwlock;

#ifdef DEBUG_RWLOCKS
	struct gbrwlock_counter_t* getCounter();
	pthread_key_t m_counter_key;
#endif
};

#endif //GB_GBRWLOCK_H
