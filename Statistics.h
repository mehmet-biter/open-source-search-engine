#ifndef GB_STATISTICS_H
#define GB_STATISTICS_H

namespace Statistics {

bool initialize();
void finalize();

void register_query_time(unsigned term_count, unsigned qlang, int error_code, unsigned ms);

void register_spider_time( bool is_new, int error_code, int http_status, unsigned ms );

void register_io_time( bool is_write, int error_code, unsigned long bytes, unsigned ms );

void register_socket_limit_hit();

} //namespace

#endif
