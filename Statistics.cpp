#include "Statistics.h"
#include "ScopedLock.h"
#include "Log.h"
#include "gb-include.h"
#include "types.h"
#include "Msg3.h"            //getDiskPageCache()
#include "RdbCache.h"
#include "Rdb.h"
#include "GbMutex.h"
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <map>
#include <set>
#include <vector>

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
static GbMutex mtx_query_timerange_statistics;



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

static void dump_query_statistics( FILE *fp ) {
	TimerangeStatistics qcopy[timerange_count][max_term_count+1];
	ScopedLock sl1(mtx_query_timerange_statistics);
	memcpy(qcopy,query_timerange_statistics,sizeof(query_timerange_statistics));
	memset(query_timerange_statistics,0,sizeof(query_timerange_statistics));
	sl1.unlock();

	for(unsigned i=0; i<timerange_count; i++) {
		for(unsigned j=1; j<max_term_count+1; j++) {
			const TimerangeStatistics &ts = qcopy[i][j];
			if ( ts.count == 0 ) {
				continue;
			}

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

//////////////////////////////////////////////////////////////////////////////
// Spidering statistics

static std::map<std::pair<int, int>, TimerangeStatistics[timerange_count]> old_spider_timerange_statistics;
static std::map<std::pair<int, int>, TimerangeStatistics[timerange_count]> new_spider_timerange_statistics;
static GbMutex mtx_spider_timerange_statistics;

void Statistics::register_spider_time( bool is_new, int error_code, int http_status, unsigned ms ) {
	{
		int i = ms_to_tr( ms );
		auto key = std::make_pair( error_code, http_status );

		ScopedLock sl( mtx_spider_timerange_statistics );
		TimerangeStatistics &ts = is_new ? new_spider_timerange_statistics.at(key)[ i ] :
		                          old_spider_timerange_statistics.at(key)[ i ];

		if ( ts.count != 0 ) {
			if ( ms < ts.min_time )
				ts.min_time = ms;
			if ( ms > ts.max_time )
				ts.max_time = ms;
		} else {
			ts.min_time = ms;
			ts.max_time = ms;
		}
		ts.count++;
		ts.sum += ms;
	}
}

enum SpiderStatistics {
	spider_doc_new = 0,
	spider_doc_changed,
	spider_doc_unchanged,
	spider_doc_deleted,
	spider_doc_disallowed,
	spider_doc_http_error,
	spider_doc_other_error,
	spider_doc_end
};

static const char* s_spider_statistics_name[] {
	"new",
	"changed",
	"unchanged",
	"deleted",
	"disallowed",
	"http_error",
	"other_error",
	""
};

static void status_to_spider_statistics( std::vector<unsigned> *spiderdoc_counts, bool is_new, int status, unsigned count ) {
	switch ( status ) {
		case 0:
			(*spiderdoc_counts)[ is_new ? spider_doc_new : spider_doc_changed ] += count;
			break;
		case EDOCUNCHANGED:
			(*spiderdoc_counts)[ spider_doc_unchanged ] += count;
			break;
		case EDOCFILTERED:
		case EDOCFORCEDELETE:
			(*spiderdoc_counts)[ spider_doc_deleted ] += count;
			break;
		case EDOCDISALLOWED:
			(*spiderdoc_counts)[ spider_doc_disallowed ] += count;
			break;
		case EDOCBADHTTPSTATUS:
			(*spiderdoc_counts)[ spider_doc_http_error ] += count;
			break;
		default:
			(*spiderdoc_counts)[ spider_doc_other_error ] += count;
			break;
	}
}

static void dump_spider_statistics( FILE *fp ) {
	ScopedLock sl1(mtx_spider_timerange_statistics);
	std::map<std::pair<int, int>, TimerangeStatistics[timerange_count]> soldcopy( old_spider_timerange_statistics );
	old_spider_timerange_statistics.clear();

	std::map<std::pair<int, int>, TimerangeStatistics[timerange_count]> snewcopy( new_spider_timerange_statistics );
	new_spider_timerange_statistics.clear();

	sl1.unlock();

	std::vector<unsigned> spiderdoc_counts( spider_doc_end );

	for ( auto it = soldcopy.begin(); it != soldcopy.end(); ++it ) {
		for ( unsigned i = 0; i < timerange_count; ++i ) {
			const TimerangeStatistics &ts = it->second[ i ];
			if ( ts.count == 0 ) {
				continue;
			}

			std::string tmp_str;
			const char *status = "SUCCESS";
			if ( it->first.first ) {
				status = merrname( it->first.first );
				if ( status == NULL ) {
					tmp_str = std::to_string( it->first.first );
					status = tmp_str.c_str();
				}
			}
			fprintf( fp, "spider:lower_bound=%u;is_new=0;status=%s;http_code=%d;min=%u;max=%u;count=%u;sum=%u\n",
			         timerange_lower_bound[ i ],
			         status,
			         it->first.second,
			         ts.min_time,
			         ts.max_time,
			         ts.count,
			         ts.sum );

			status_to_spider_statistics( &spiderdoc_counts, false, it->first.first, ts.count );
		}
	}

	for ( auto it = snewcopy.begin(); it != snewcopy.end(); ++it ) {
		for ( unsigned i = 0; i < timerange_count; ++i ) {
			const TimerangeStatistics &ts = it->second[ i ];
			if ( ts.count == 0 ) {
				continue;
			}

			const char *status = it->first.first ? ( merrname( it->first.first ) ?: std::to_string( it->first.first ).c_str() ) : "SUCCESS";
			fprintf( fp, "spider:lower_bound=%u;is_new=1;status=%s;http_code=%d;min=%u;max=%u;count=%u;sum=%u\n",
			         timerange_lower_bound[ i ],
			         status,
			         it->first.second,
			         ts.min_time,
			         ts.max_time,
			         ts.count,
			         ts.sum );

			status_to_spider_statistics( &spiderdoc_counts, true, it->first.first, ts.count );
		}
	}

	for ( unsigned i = 0; i < spider_doc_end; ++i ) {
		unsigned count = spiderdoc_counts[ i ];
		if ( count > 0 ) {
			fprintf( fp, "spiderdoc:%s=%u\n", s_spider_statistics_name[ i ], count );
		}
	}
}

//////////////////////////////////////////////////////////////////////////////
// RdbCache statistics

// RdbCache keeps its own statistics so we just pull those out

struct RdbCacheHistory {
	rdbid_t rdb_id;
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
	{RDB_NONE,0,0,0}
};

static void dump_rdb_cache_statistics( FILE *fp ) {
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

//////////////////////////////////////////////////////////////////////////////
// statistics

static void dump_statistics(time_t now) {
	FILE *fp = fopen(tmp_filename,"w");
	if ( !fp ) {
		log( LOG_ERROR, "fopen(%s,\"w\") failed with errno=%d (%s)", tmp_filename, errno, strerror( errno ) );
		return;
	}

	fprintf( fp, "%ld\n", ( long ) now );

	// dump statistics
	dump_query_statistics( fp );
	dump_spider_statistics( fp );
	dump_rdb_cache_statistics( fp );
	
	if ( fflush(fp) != 0 ) {
		log( LOG_ERROR, "fflush(%s) failed with errno=%d (%s)", tmp_filename, errno, strerror( errno ) );
		fclose( fp );
		return;
	}
	fclose(fp);
	
	if ( rename( tmp_filename, final_filename ) != 0 ) {
		log( LOG_ERROR, "rename(%s,%s) failed with errno=%d (%s)", tmp_filename, final_filename, errno, strerror( errno ) );
	}
}




static bool stop_dumping = false;
static GbMutex mtx_dump;
static pthread_cond_t cond_dump = PTHREAD_COND_INITIALIZER;
static pthread_t dump_thread;

extern "C" {

static void *dumper_thread_function(void *)
{
	mtx_dump.lock();
	while(!stop_dumping) {
		timespec ts;
		clock_gettime(CLOCK_REALTIME,&ts);
		ts.tv_sec += dump_interval;
		ts.tv_sec = (ts.tv_sec/dump_interval)*dump_interval;
		pthread_cond_timedwait(&cond_dump,&mtx_dump.mtx,&ts);
		if(stop_dumping)
			break;
		mtx_dump.unlock();
		clock_gettime(CLOCK_REALTIME,&ts);
		dump_statistics(ts.tv_sec);
		mtx_dump.lock();
	}
	mtx_dump.unlock();
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
