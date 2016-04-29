#include "JobScheduler.h"
#include "Conf.h"
#include "Mem.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

static void start_routine(void *) {
}
static void finish_routine(void *, job_exit_t) {
}

int main(void) {
	g_conf.m_maxMem = 1000000000LL;
	g_mem.m_memtablesize = 8194*1024;
	g_mem.init();
	
	//verify JobScheduler::are_new_jobs_allowed() works
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
		
		js.disallow_new_jobs();
		b = js.submit(start_routine,
		              finish_routine,
		              0, //state
		              thread_type_query_intersect,
		              0, //priority/niceness
		              0  //start_deadline
		             );
		assert(!b);
		js.allow_new_jobs();
		b = js.submit(start_routine,
		              finish_routine,
		              0, //state
		              thread_type_query_intersect,
		              0, //priority/niceness
		              0  //start_deadline
		             );
		assert(b);
		
		js.cleanup_finished_jobs();
		
		js.finalize();
	}
	
	
	printf("success\n");
	return 0;
}
