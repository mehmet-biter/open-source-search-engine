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

static unsigned jobs_started = 0;
static void start_routine(void *) {
	jobs_started++;
	msleep(200);
}
static unsigned jobs_finished_with_cancel = 0;
static void finish_routine(void *, job_exit_t job_exit) {
	if(job_exit==job_exit_cancelled)
		jobs_finished_with_cancel++;
}

int main(void) {
	g_conf.m_maxMem = 1000000000LL;
	g_mem.m_memtablesize = 8194*1024;
	g_mem.init();
	
	//verify that cancel_file_read_jobs() works
	{
		JobScheduler js;
		js.initialize(1,1,1);
		
		BigFile *bf = (BigFile*)0x1234;
		FileState fstate;
		fstate.m_this = bf;
		js.submit_io(start_routine,
		             finish_routine,
		             &fstate, //state
		             thread_type_query_read,
		             0, //priority/niceness
		             false, //is_write
			     0  //start_deadline
		            );
		js.submit_io(start_routine,
		             finish_routine,
		             &fstate, //state
		             thread_type_query_read,
		             0, //priority/niceness
		             false, //is_write
			     0  //start_deadline
		            );
		js.submit_io(start_routine,
		             finish_routine,
		             &fstate, //state
		             thread_type_query_read,
		             0, //priority/niceness
		             true, //is_write
			     0  //start_deadline
		            );
		msleep(100);
		
		//1st job: already started
		//2nd job: in queue, read job, cancelled
		//3rd job: in queue, write job, not cancelled
		js.cancel_file_read_jobs(bf);
		
		msleep(400);
		
		js.cleanup_finished_jobs();
		
		assert(jobs_started==1);
		assert(jobs_finished_with_cancel==1);
		
		js.cleanup_finished_jobs();
		js.finalize();
	}
	
	
	printf("success\n");
	return 0;
}
