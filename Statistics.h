#ifndef STATISTICS_H__
#define STATISTICS_H__

namespace Statistics {

bool initialize();
void finalize();

void register_query_time(unsigned term_count, unsigned qlang, unsigned ms);

void register_spider_time(unsigned ms, int resultcode);
void register_spider_old_links(unsigned count);
void register_spider_new_links(unsigned count);

} //namespace

#endif
