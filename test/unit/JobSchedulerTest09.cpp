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
	
	//verify that is_reading_file() works
	{
		JobScheduler js;
		js.initialize(1,1,1);
		
		BigFile *bf1 = (BigFile*)0x1234;
		BigFile *bf2 = (BigFile*)0x12347;
		FileState fstate1;
		fstate1.m_this = bf1;
		FileState fstate2;
		fstate2.m_this = bf2;
		
		assert(!js.is_reading_file(bf1));
		assert(!js.is_reading_file(bf2));
		
		js.submit_io(start_routine,
		             finish_routine,
		             &fstate1, //state
		             thread_type_query_read,
		             0, //priority/niceness
		             false, //is_write
			     0  //start_deadline
		            );
		js.submit_io(start_routine,
		             finish_routine,
		             &fstate2, //state
		             thread_type_query_read,
		             0, //priority/niceness
		             false, //is_write
			     0  //start_deadline
		            );
		assert(js.is_reading_file(bf1));
		assert(js.is_reading_file(bf2));
		msleep(100);
		assert(js.is_reading_file(bf1));
		assert(js.is_reading_file(bf2));
		msleep(200);
		assert(!js.is_reading_file(bf1));
		assert(js.is_reading_file(bf2));
		
		msleep(200);
		assert(!js.is_reading_file(bf1));
		assert(!js.is_reading_file(bf2));
		
		js.cleanup_finished_jobs();
		js.finalize();
	}
	
	
	printf("success\n");
	return 0;
}
