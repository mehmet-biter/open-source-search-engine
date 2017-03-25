#include "GbThreadQueue.h"
#include "ScopedLock.h"
#include <stdexcept>
#include "Log.h"
void* GbThreadQueue::thread_queue_function(void *args) {
	GbThreadQueue *tq = static_cast<GbThreadQueue*>(args);

	while (!tq->m_stop) {
		ScopedLock sl(tq->m_queueMtx);
		if (tq->m_queue.empty()) {
			pthread_cond_signal(&tq->m_queueCondEmpty);
			pthread_cond_wait(&tq->m_queueCondNotEmpty, &tq->m_queueMtx);
		}

		if (tq->m_stop) {
			break;
		}

		// spurious wakeup
		if (tq->m_queue.empty()) {
			continue;
		}

		void *item = tq->m_queue.front();
		sl.unlock();

		// process item
		tq->m_func(item);

		ScopedLock sl2(tq->m_queueMtx);
		tq->m_queue.pop();

		/// @todo ALC configurable delay here?
	}

	return NULL;
}

GbThreadQueue::GbThreadQueue()
	: m_queue()
	, m_queueMtx(PTHREAD_MUTEX_INITIALIZER)
	, m_queueCondEmpty(PTHREAD_COND_INITIALIZER)
	, m_queueCondNotEmpty(PTHREAD_COND_INITIALIZER)
	, m_thread()
	, m_func()
	, m_stop(false)
	, m_started(false) {
}

GbThreadQueue::~GbThreadQueue() {
	finalize();
}

bool GbThreadQueue::initialize(queue_func_t processFunc, const char *threadName) {
	m_func = processFunc;

	if (pthread_create(&m_thread, NULL, thread_queue_function, this) != 0) {
		return false;
	}

	char threadNameBuf[16]; //hard limit
	snprintf(threadNameBuf,sizeof(threadNameBuf),"%s", threadName);
	if (pthread_setname_np(m_thread, threadNameBuf) != 0) {
		finalize();
		return false;
	}

	m_started = true;

	return true;
}

void GbThreadQueue::finalize() {
	if (m_started) {
		m_stop = true;
		pthread_cond_broadcast(&m_queueCondNotEmpty);
		pthread_join(m_thread, NULL);
		m_started = false;
	}
}

void GbThreadQueue::addItem(void *item) {
	ScopedLock sl(m_queueMtx);
	m_queue.push(item);
	pthread_cond_signal(&m_queueCondNotEmpty);
}

bool GbThreadQueue::isEmpty() {
	ScopedLock sl(m_queueMtx);
	return m_queue.empty();
}

bool GbThreadQueue::waitUntilEmpty() {
	ScopedLock sl(m_queueMtx);
	if (m_queue.empty()) {
		return true;
	}

	while (!m_stop && !m_queue.empty()) {
		pthread_cond_wait(&m_queueCondEmpty, &m_queueMtx);
	}

	return m_queue.empty();
}
