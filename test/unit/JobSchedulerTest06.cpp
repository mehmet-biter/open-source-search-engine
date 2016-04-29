#include "JobScheduler.h"
#include "Conf.h"
#include "Mem.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

static uint64_t now_ms() {
	struct timeval tv;
	gettimeofday(&tv,0);
	return tv.tv_sec*1000 + tv.tv_usec/1000;
}
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
static bool job_timed_out = false;
static void finish_routine(void *, job_exit_t job_exit) {
	if(job_exit==job_exit_deadline)
		job_timed_out = true;
}

int main(void) {
	g_conf.m_maxMem = 1000000000LL;
	g_mem.m_memtablesize = 8194*1024;
	g_mem.init();
	
	//verify that jobs time out
	{
		JobScheduler js;
		js.initialize(1,1,1);
		
		js.submit(start_routine,
		          finish_routine,
		          0, //state
		          thread_type_query_intersect,
		          0, //priority/niceness
		          now_ms()+200  //start_deadline
		         );
		js.submit(start_routine,
		          finish_routine,
		          0, //state
		          thread_type_query_intersect,
		          0, //priority/niceness
		          now_ms()+200  //start_deadline
		         );
		msleep(250);
		
		proceed=true;
		pthread_cond_broadcast(&cond);
		
		msleep(200);
		
		js.cleanup_finished_jobs();
		
		assert(job_timed_out);
		
		js.finalize();
	}
	
	
	printf("success\n");
	return 0;
}
