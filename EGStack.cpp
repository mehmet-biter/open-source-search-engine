#include "EGStack.h"


void *EGStack::alloc(size_t bytes) {
	if(chunks.empty() || lastest_chunk_used+bytes > lastest_chunk_size) {
		chunks.reserve(chunks.size()+10);
		size_t cs = bytes<=chunk_size ? chunk_size : bytes;
		char *new_chunk = new char[cs];
		chunks.push_back(new_chunk);
		lastest_chunk_size = cs;
		lastest_chunk_used = 0;
	}
	
	char *ptr = chunks.back() + lastest_chunk_used;
	lastest_chunk_used += bytes;
	return ptr;
}

void EGStack::clear() {
	for(auto e : chunks)
		delete[] e;
	chunks.clear();
	lastest_chunk_size = 0;
	lastest_chunk_used = 0;
}
