#ifndef STATISTICS_H__
#define STATISTICS_H__

namespace Statistics {

bool initialize();
void finalize();

void register_query_time(unsigned term_count, unsigned qlang, unsigned ms);

void register_spider_time( bool is_new, int error_code, int http_status, unsigned ms );

} //namespace

#endif
