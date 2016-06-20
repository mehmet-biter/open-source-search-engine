#include "Statistics.h"
#include "ScopedLock.h"
#include "Log.h"
#include "gb-include.h"
#include "types.h"
#include "Msg3.h"            //getDiskPageCache()
#include "RdbCache.h"
#include "Rdb.h"
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


static unsigned ms_to_tr(unsigned ms) {
	unsigned i=timerange_count-1;
	while(ms<timerange_lower_bound[i])
		i--;
	return i;
}

//////////////////////////////////////////////////////////////////////////////
// Query statistics

static TimerangeStatistics query_timerange_statistics[timerange_count][max_term_count+1];
static pthread_mutex_t mtx_query_timerange_statistics = PTHREAD_MUTEX_INITIALIZER;



void Statistics::register_query_time(unsigned term_count, unsigned /*qlang*/, unsigned ms)
{
	if(term_count>max_term_count)
		term_count = max_term_count;
	
	unsigned i=ms_to_tr(ms);
	
	ScopedLock sl(mtx_query_timerange_statistics);
	TimerangeStatistics &ts = query_timerange_statistics[i][term_count];
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


//////////////////////////////////////////////////////////////////////////////
// Spidering statistics

static TimerangeStatistics spider_timerange_statistics[timerange_count][2]; //1=success, 0=anything else
static pthread_mutex_t mtx_spider_timerange_statistics = PTHREAD_MUTEX_INITIALIZER;
static unsigned old_links_found=0;
static unsigned new_links_found=0;
static unsigned changed_documents_spidered=0;
static unsigned unchanged_documents_spidered=0;
static pthread_mutex_t mtx_spider_count = PTHREAD_MUTEX_INITIALIZER;

void register_spider_time(unsigned ms, int resultcode)
{
	unsigned i=ms_to_tr(ms);
	unsigned j=resultcode==200 ? 1 : 0;
	
	ScopedLock sl(mtx_spider_timerange_statistics);
	TimerangeStatistics &ts = spider_timerange_statistics[i][j];
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


void register_spider_old_links(unsigned count)
{
	ScopedLock sl(mtx_spider_count);
	old_links_found++;
}

void register_spider_new_links(unsigned )
{
	ScopedLock sl(mtx_spider_count);
	new_links_found++;
}

void register_spider_changed_document(unsigned count)
{
	ScopedLock sl(mtx_spider_count);
	changed_documents_spidered++;
}

void register_spider_unchanged_document(unsigned count)
{
	ScopedLock sl(mtx_spider_count);
	changed_documents_spidered++;
}


//////////////////////////////////////////////////////////////////////////////
// RdbCache statistics

// RdbCache keeps its own statistics so we just pull those out

struct RdbCacheHistory {
	int rdb_id;
	const char *name;
	int64_t last_hits;
	int64_t last_misses;
};
static RdbCacheHistory rdb_cache_history[] = {
	{RDB_POSDB,    "posdb",    0,0},
	{RDB_TAGDB,    "tagdb",    0,0},
	{RDB_CLUSTERDB,"clusterdb",0,0},
	{RDB_TITLEDB,  "titledb",  0,0},
	{RDB_SPIDERDB, "spiderdb", 0,0},
	{0,0,0,0}
};


static void dump_rdb_cache_statistics(FILE *fp)
{
	for(int i=0; rdb_cache_history[i].name; i++) {
		const RdbCache *c = getDiskPageCache(rdb_cache_history[i].rdb_id);
		if(!c)
			continue;
		int64_t delta_hits = c->getNumHits() - rdb_cache_history[i].last_hits;
		int64_t delta_misses = c->getNumMisses() - rdb_cache_history[i].last_misses;
		rdb_cache_history[i].last_hits = c->getNumHits();
		rdb_cache_history[i].last_misses = c->getNumMisses();
		
		fprintf(fp,"rdbcache:%s;hits=%" PRId64 ";misses=%" PRId64 "\n", rdb_cache_history[i].name,delta_hits,delta_misses);
	}
}


static void dump_statistics(time_t now)
{
	FILE *fp = fopen(tmp_filename,"w");
	if(!fp) {
		log(LOG_ERROR,"fopen(%s,\"w\") failed with errno=%d (%s)", tmp_filename, errno, strerror(errno));
		return;
	}
	
	fprintf(fp,"%ld\n", (long)now);
	
	TimerangeStatistics qcopy[timerange_count][max_term_count+1];
	ScopedLock sl1(mtx_query_timerange_statistics);
	memcpy(qcopy,query_timerange_statistics,sizeof(query_timerange_statistics));
	memset(query_timerange_statistics,0,sizeof(query_timerange_statistics));
	sl1.unlock();
	
	for(unsigned i=0; i<timerange_count; i++) {
		for(unsigned j=1; j<max_term_count+1; j++) {
			const TimerangeStatistics &ts = qcopy[i][j];
			if(ts.count!=0) {
				fprintf(fp,"query:lower_bound=%u;terms=%u;min=%u;max=%u;count=%u;sum=%u\n",
					timerange_lower_bound[i],
					j,
					ts.min_time,
					ts.max_time,
					ts.count,
					ts.sum);
			}
		}
	}
	
	TimerangeStatistics scopy[timerange_count][2];
	ScopedLock sl2(mtx_spider_timerange_statistics);
	memcpy(scopy,spider_timerange_statistics,sizeof(spider_timerange_statistics));
	memset(spider_timerange_statistics,0,sizeof(spider_timerange_statistics));
	sl2.unlock();
	
	for(unsigned i=0; i<timerange_count; i++) {
		for(unsigned j=0; j<2; j++) {
			const TimerangeStatistics &ts = scopy[i][j];
			if(ts.count!=0) {
				fprintf(fp,"spider:lower_bound=%u;code=%d;min=%u;max=%u;count=%u;sum=%u\n",
					timerange_lower_bound[i],
					j?200:0,
					ts.min_time,
					ts.max_time,
					ts.count,
					ts.sum);
			}
		}
	}
	
	ScopedLock sl3(mtx_spider_count);
	unsigned copy_old_links_found = old_links_found;
	unsigned copy_new_links_found = new_links_found;
	unsigned copy_changed_documents_spidered = changed_documents_spidered;
	unsigned copy_unchanged_documents_spidered = unchanged_documents_spidered;
	old_links_found = 0;
	new_links_found = 0;
	changed_documents_spidered = 0;
	unchanged_documents_spidered = 0;
	sl3.unlock();
	
	fprintf(fp,"spiderlinks:old:count=%u\n",copy_old_links_found);
	fprintf(fp,"spiderlinks:new:count=%u\n",copy_new_links_found);
	fprintf(fp,"spiderlinks:unchanged:count=%u\n",copy_changed_documents_spidered);
	fprintf(fp,"spiderlinks:changed:count=%u\n",copy_unchanged_documents_spidered);
	
	dump_rdb_cache_statistics(fp);
	
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
