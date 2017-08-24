#ifndef GB_STATISTICS_H
#define GB_STATISTICS_H

#include <cstdint>

namespace Statistics {

bool initialize();
void finalize();

void register_query_time(unsigned term_count, unsigned qlang, int error_code, unsigned ms);

void register_spider_time( bool is_new, int error_code, int http_status, unsigned ms );

void register_io_time( bool is_write, int error_code, unsigned long bytes, unsigned ms );

void register_document_encoding(int error_code, int16_t charsetId, uint8_t langId, uint16_t countryId);

void register_socket_limit_hit();

void increment_url_block_counter_call();
void increment_url_block_counter_blacklisted();
void increment_url_block_counter_whitelisted();
void increment_url_block_counter_shlib_domain_block();
void increment_url_block_counter_shlib_url_block();
void increment_url_block_counter_default();

} //namespace

#endif
