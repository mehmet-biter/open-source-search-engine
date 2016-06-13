#include "Statistics.h"
#include "ScopedLock.h"
#include "Log.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static const time_t dump_interval = 60;
static const char tmp_filename[] = "statistics.txt.new";
static const char final_filename[] = "statistics.txt";

static const size_t max_term_count = 10;


static const unsigned timerange_lower_bound[] = {
		0,
		10,
		20,
		50,
		100,
		200,
		500,
		1000,
		2000,
		5000,
		10000,
		20000
};

static const size_t timerange_count = sizeof(timerange_lower_bound)/sizeof(timerange_lower_bound[0]);


struct TimerangeStatistics {
	unsigned min_time;
	unsigned max_time;
	unsigned count;
	unsigned sum;
};


static TimerangeStatistics timerange_statistics[timerange_count][max_term_count+1];
static pthread_mutex_t mtx_timerange_statistics = PTHREAD_MUTEX_INITIALIZER;



void Statistics::register_query_time(unsigned term_count, unsigned /*qlang*/, unsigned ms)
{
	if(term_count>max_term_count)
		term_count = max_term_count;
	
	unsigned i=timerange_count-1;
	while(ms<timerange_lower_bound[i])
		i--;
	
	ScopedLock sl(mtx_timerange_statistics);
	TimerangeStatistics &ts = timerange_statistics[i][term_count];
	if(ts.count!=0) {
		if(ms<ts.min_time)
			ts.min_time = ms;
		if(ms>ts.max_time)
			ts.max_time = ms;
	} else {
		ts.min_time = ms;
		ts.max_time = ms;
	}
	ts.count++;
	ts.sum += ms;
}



static void dump_statistics(time_t now)
{
	FILE *fp = fopen(tmp_filename,"w");
	if(!fp) {
		log(LOG_ERROR,"fopen(%s,\"w\") failed with errno=%d (%s)", tmp_filename, errno, strerror(errno));
		return;
	}
	
	TimerangeStatistics copy[timerange_count][max_term_count+1];
	ScopedLock sl(mtx_timerange_statistics);
	memcpy(copy,timerange_statistics,sizeof(timerange_statistics));
	memset(timerange_statistics,0,sizeof(timerange_statistics));
	sl.unlock();
	
	for(unsigned i=0; i<timerange_count; i++) {
		for(unsigned j=1; j<max_term_count+1; j++) {
			const TimerangeStatistics &ts = copy[i][j];
			if(ts.count!=0) {
				fprintf(fp,"lower_bound=%u;terms=%u;min=%u;max=%u;count=%u;sum=%u\n",
					timerange_lower_bound[i],
					j,
					ts.min_time,
					ts.max_time,
					ts.count,
					ts.sum);
			}
		}
	}
	if(fflush(fp)!=0) {
		log(LOG_ERROR,"fflush(%s) failed with errno=%d (%s)", tmp_filename, errno, strerror(errno));
		fclose(fp);
		return;
	}
	fclose(fp);
	
	if(rename(tmp_filename, final_filename)!=0) {
		log(LOG_ERROR,"rename(%s,%s) failed with errno=%d (%s)", tmp_filename, final_filename, errno, strerror(errno));
	}
}




static bool stop_dumping = false;
static pthread_mutex_t mtx_dump = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_dump = PTHREAD_COND_INITIALIZER;
static pthread_t dump_thread;

extern "C" {

static void *dumper_thread_function(void *)
{
	ScopedLock sl(mtx_dump);
	while(!stop_dumping) {
		timespec ts;
		clock_gettime(CLOCK_REALTIME,&ts);
		ts.tv_sec += dump_interval;
		ts.tv_sec = (ts.tv_sec/dump_interval)*dump_interval;
		pthread_cond_timedwait(&cond_dump,&mtx_dump,&ts);
		if(stop_dumping)
			break;
		clock_gettime(CLOCK_REALTIME,&ts);
		dump_statistics(ts.tv_sec);
	}
	return 0;
}

} //extern C




bool Statistics::initialize()
{
	int rc = pthread_create(&dump_thread, NULL, dumper_thread_function, NULL);
	if(rc!=0) {
		log(LOG_ERROR,"pthread_create() failed with rc=%d (%s)",rc,strerror(rc));
		return false;
	}
	return true;
}


void Statistics::finalize()
{
	stop_dumping = true;
	pthread_cond_signal(&cond_dump);
	pthread_join(dump_thread,NULL);
}
