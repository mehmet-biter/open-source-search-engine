#include "JobScheduler.h"
#include "Conf.h"
#include "Mem.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
	g_conf.m_maxMem = 1000000000LL;
	g_mem.m_memtablesize = 8194*1024;
	g_mem.init();
	
	//test init/finit which involves thraed create/join
	{
		JobScheduler js;
		js.initialize(1,1,1);
		js.finalize();
	}
	
	
	printf("success\n");
	return 0;
}
