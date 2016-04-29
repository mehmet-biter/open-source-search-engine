#include "JobScheduler.h"
#include "Conf.h"
#include "Mem.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

static void msleep(int msecs) {
	struct timespec ts;
	ts.tv_sec = msecs/1000;
	ts.tv_nsec = (msecs%1000)*1000000;
	nanosleep(&ts,NULL);
}

static bool start_routine_called = false;
static bool finish_routine_called = false;
static void start_routine(void *) {
	printf("start_routine() called\n");
	start_routine_called = true;
}
static void finish_routine(void *, job_exit_t) {
	printf("finish_routine() called\n");
	finish_routine_called = true;
}



int main(void) {
	g_conf.m_maxMem = 1000000000LL;
	g_mem.m_memtablesize = 8194*1024;
	g_mem.init();
	
	//verify the lifecycle of a simple job
	{
		JobScheduler js;
		js.initialize(1,1,1);
		
		bool b;
		b = js.submit(start_routine,
		              finish_routine,
		              0, //state
		              thread_type_query_intersect,
		              0, //priority/niceness
		              0  //start_deadline
		             );
		assert(b);
		
		msleep(500);
		assert(start_routine_called);
		msleep(500);
		assert(!finish_routine_called);
		js.cleanup_finished_jobs();
		assert(finish_routine_called);
		
		js.finalize();
	}
	
	
	printf("success\n");
	return 0;
}
