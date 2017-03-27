#ifndef GB_GBTHREADQUEUE_H
#define GB_GBTHREADQUEUE_H

#include <queue>
#include <pthread.h>


typedef void (*queue_func_t)(void *item);

class GbThreadQueue {
public:
	GbThreadQueue();
	~GbThreadQueue();

	bool initialize(queue_func_t processFunc, const char *threadName);
	void finalize();

	// we are not responsible for cleaning up memory used by item
	void addItem(void *item);

	bool isEmpty();
private:
	static void* thread_queue_function(void *args);

	std::queue<void*> m_queue;
	pthread_mutex_t m_queueMtx;
	pthread_cond_t m_queueCondNotEmpty;

	pthread_t m_thread;
	queue_func_t m_func;
	bool m_stop;
	bool m_started;
};

#endif //GB_GBTHREADQUEUE_H
