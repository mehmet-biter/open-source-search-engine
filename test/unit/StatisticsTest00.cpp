#include "Statistics.h"
#include <unistd.h>
#include <assert.h>
#include <stdio.h>

#include "Conf.h"
#include "Mem.h"


int main(void) {
	g_conf.m_maxMem = 1000000000LL;
	g_mem.m_memtablesize = 8194*1024;
	g_mem.init();
	unlink("statistics.txt");
	assert(Statistics::initialize());
	
	Statistics::register_query_time(1, 1, 13);

	Statistics::register_query_time(2, 2, 21);
	Statistics::register_query_time(2, 2, 28);
	
	Statistics::register_query_time(15, 0, 25000);
	
	sleep(60+1);
	
	assert(access("statistics.txt",R_OK)==0);
	
	Statistics::finalize();
	
	//verify content
	FILE *fp=fopen("statistics.txt","r");
	assert(fp);
	unsigned lower_bound;
	unsigned terms;
	unsigned min;
	unsigned max;
	unsigned count;
	unsigned sum;
	assert(fscanf(fp,"lower_bound=%u;terms=%u;min=%u;max=%u;count=%u;sum=%u\n",&lower_bound,&terms,&min,&max,&count,&sum)==6);
	assert(lower_bound==10);
	assert(terms==1);
	assert(min==13);
	assert(max==13);
	assert(count==1);
	assert(sum==13);

	assert(fscanf(fp,"lower_bound=%u;terms=%u;min=%u;max=%u;count=%u;sum=%u\n",&lower_bound,&terms,&min,&max,&count,&sum)==6);
	assert(lower_bound==20);
	assert(terms==2);
	assert(min==21);
	assert(max==28);
	assert(count==2);
	assert(sum==49);

	assert(fscanf(fp,"lower_bound=%u;terms=%u;min=%u;max=%u;count=%u;sum=%u\n",&lower_bound,&terms,&min,&max,&count,&sum)==6);
	assert(lower_bound==20000);
	assert(terms==10);
	assert(min==25000);
	assert(max==25000);
	assert(count==1);
	assert(sum==25000);

	return 0;
}
