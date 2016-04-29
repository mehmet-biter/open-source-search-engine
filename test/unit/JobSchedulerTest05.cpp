#include "JobScheduler.h"
#include "Conf.h"
#include "Mem.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

static void msleep(int msecs) {
	struct timespec ts;
	ts.tv_sec = msecs/1000;
	ts.tv_nsec = (msecs%1000)*1000000;
	nanosleep(&ts,NULL);
}

static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static bool proceed = false;
static void start_routine(void *) {
	pthread_mutex_lock(&mtx);
	while(!proceed)
		pthread_cond_wait(&cond,&mtx);
	pthread_mutex_unlock(&mtx);
}
static void finish_routine(void *, job_exit_t) {
}

int main(void) {
	g_conf.m_maxMem = 1000000000LL;
	g_mem.m_memtablesize = 8194*1024;
	g_mem.init();
	
	//verify JobScheduler::num_queued_jobs() works reasonably
	{
		JobScheduler js;
		js.initialize(10,10,10);
		
		for(int r=0; r<100; r++) {
			js.submit(start_routine,
			          finish_routine,
			          0, //state
			          thread_type_query_intersect,
			          0, //priority/niceness
			          0  //start_deadline
			         );
		}
		msleep(200);
		assert(js.num_queued_jobs()<=90);
		assert(js.num_queued_jobs()>=70);
		
		proceed=true;
		pthread_cond_broadcast(&cond);
		
		msleep(500);
		
		js.cleanup_finished_jobs();
		assert(js.num_queued_jobs()==0);
		
		js.finalize();
	}
	
	
	printf("success\n");
	return 0;
}
