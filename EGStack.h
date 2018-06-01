#ifndef EGSTACK_H_
#define EGSTACK_H_
#include <stddef.h>
#include <vector>

//An ever-growing stack. Or an obstack with no individual element deallocation.
//You construct the egstack, allocate several items from it and then free all in one go.
class EGStack {
	EGStack(const EGStack&) = delete;
	EGStack& operator=(const EGStack&) = delete;
public:
	EGStack(size_t chunk_size_ = 4096)
	  : chunks(),
	    lastest_chunk_size(0),
	    lastest_chunk_used(0),
	    chunk_size(chunk_size_)
	  {}
	~EGStack() { clear(); }
	
	void *alloc(size_t bytes);
	void clear();

private:
	std::vector<char *> chunks;
	size_t lastest_chunk_size;
	size_t lastest_chunk_used;
	const size_t chunk_size;
};

#endif
