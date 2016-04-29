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

static void start_routine(void *) {
	msleep(200);
}
static void finish_routine(void *, job_exit_t) {
}

int main(void) {
	g_conf.m_maxMem = 1000000000LL;
	g_mem.m_memtablesize = 8194*1024;
	g_mem.init();
	
	//verify that is_io_write_jobs_running() works
	{
		JobScheduler js;
		js.initialize(1,1,1);
		
		FileState fstate;
		js.submit_io(start_routine,
		             finish_routine,
		             &fstate, //state
		             thread_type_query_read,
		             0, //priority/niceness
		             true, //is_write
			     0  //start_deadline
		            );
		msleep(100);
		
		assert(js.are_io_write_jobs_running());
		
		msleep(200);
		
		js.cleanup_finished_jobs();
		
		js.submit_io(start_routine,
		             finish_routine,
		             &fstate, //state
		             thread_type_query_read,
		             0, //priority/niceness
		             false, //is_write
			     0  //start_deadline
		            );
		msleep(100);
		
		assert(!js.are_io_write_jobs_running());
		
		msleep(200);
		
		js.cleanup_finished_jobs();
		js.finalize();
	}
	
	
	printf("success\n");
	return 0;
}
