#ifndef MEMORYMAPPEDFILE_H_
#define MEMORYMAPPEDFILE_H_

#include <stddef.h>


class MemoryMappedFile {
	void *pv;
	size_t sz;

	MemoryMappedFile(const MemoryMappedFile&) = delete;
	MemoryMappedFile& operator=(const MemoryMappedFile) = delete;

public:
	MemoryMappedFile() : pv(0), sz(0) {}
	MemoryMappedFile(const char *filename, bool for_write=false)
	  : pv(0), sz(0)
	{
		open(filename,for_write);
	}
	~MemoryMappedFile() { close(); }
	
	bool open(const char *filename, bool for_write=false);
	void close();
	
	bool is_open() const { return pv!=0; }
	
	char       *start()       { return (char*)pv; }
	const char *start() const { return (const char*)pv; }
	size_t size() const { return sz; }
	
	void advise_normal() const;
	void advise_random() const;
	void advice_sequential() const;
	void advise_willneed() const;
	void advise_dontneed() const;

private:
	void advise(int advice) const;
};


#endif
