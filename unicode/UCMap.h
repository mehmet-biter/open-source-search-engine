#ifndef UCMAP_H_
#define UCMAP_H_
#include <inttypes.h>
#include <stddef.h>
#include <map>

typedef uint32_t  UChar32;

//Mappings from a unicode codepoint (Char32) to <whatever>

namespace UnicodeMaps {


template<class V>
class FullMap {
	FullMap(const FullMap&) = delete;
	FullMap& operator=(const FullMap&) = delete;
public:
	FullMap() : value(nullptr), values(0) {}
	~FullMap() { clear(); }
	
	void clear();
	
	bool empty() const { return values==0; }
	size_t size() const { return (size_t)values; }
	
	const V *lookup(UChar32 c) const {
		if(c<values)
			return &(value[c]);
		else
			return nullptr;
	}
	V lookup2(UChar32 c) const {
		if(c<values)
			return value[c];
		else
			return (V)0;
	}
	
	bool load(const char *filename);
private:
	const V *value;
	UChar32 values;
	size_t bytes;
};


template<class V>
class SparseMap {
	SparseMap(const SparseMap&) = delete;
	SparseMap& operator=(const SparseMap&) = delete;
public:
	SparseMap() : m(), mmap_ptr(nullptr), bytes(0) {}
	~SparseMap() { clear(); }
	
	void clear();
	
	bool empty() const { return m.empty(); }
	size_t size() const { return m.size(); }
	
	struct Entry {
		uint32_t count;
		V values[];
	};
	const Entry *lookup(UChar32 c) const {
		auto iter = m.find(c);
		if(iter!=m.end())
			return iter->second;
		else
			return nullptr;
	}
	
	bool load(const char *filename);
private:
	std::map<UChar32,const Entry*> m;
	void *mmap_ptr;
	size_t bytes;
};


} //namespace

#endif
