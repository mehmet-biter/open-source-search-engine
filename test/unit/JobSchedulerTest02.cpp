#include "JobScheduler.h"
#include "Conf.h"
#include "Mem.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
	g_conf.m_maxMem = 1000000000LL;
	g_mem.m_memtablesize = 8194*1024;
	g_mem.init();
	
	//test simply query methods and initial/default values
	{
		JobScheduler js;
		js.initialize(1,1,1);
		
		assert(!js.is_io_write_jobs_running());
		assert(!js.is_reading_file((BigFile*)0x01));
		assert(js.are_new_jobs_allowed());
		assert(js.num_queued_jobs() == 0);
		
		js.finalize();
	}
	
	
	printf("success\n");
	return 0;
}
