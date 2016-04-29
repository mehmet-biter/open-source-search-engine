#include "JobScheduler.h"
#include "Conf.h"
#include "Mem.h"
#include "BigFile.h"
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
static bool job_a_started=false;
static void start_routine_a(void *) {
	job_a_started = true;
	msleep(200);
}
static bool job_b_started=false;
static void start_routine_b(void *) {
	job_b_started = true;
}
static bool job_c_started=false;
static void start_routine_c(void *) {
	job_c_started = true;
	msleep(200);
}
static void finish_routine(void *, job_exit_t) {
}

int main(void) {
	g_conf.m_maxMem = 1000000000LL;
	g_mem.m_memtablesize = 8194*1024;
	g_mem.init();
	
	//verify that job priorities/niceness work
	{
		JobScheduler js;
		js.initialize(1,1,1);
		
		js.submit(start_routine_a,
		          finish_routine,
		          NULL, //state
		          thread_type_query_intersect,
		          0, //priority/niceness
			  0  //start_deadline
		         );
		js.submit(start_routine_b,
		          finish_routine,
		          NULL, //state
		          thread_type_query_intersect,
		          17, //priority/niceness
			  0  //start_deadline
		         );
		js.submit(start_routine_c,
		          finish_routine,
		          NULL, //state
		          thread_type_query_intersect,
		          0, //priority/niceness
			  0  //start_deadline
		         );
		
		msleep(50);
		assert(job_a_started);
		assert(!job_b_started);
		assert(!job_c_started);
		msleep(200);
		assert(job_a_started);
		assert(!job_b_started);
		assert(job_c_started);
		msleep(200);
		assert(job_b_started);

		js.cleanup_finished_jobs();
		js.finalize();
	}
	
	
	printf("success\n");
	return 0;
}
