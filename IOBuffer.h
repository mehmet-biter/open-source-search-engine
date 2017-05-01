#ifndef IOBUFFER_H_
#define IOBUFFER_H_

#include <string.h>
#include <algorithm>


class IOBuffer {
	char *buf;
	size_t bufused;
	size_t bufsize;

public:
	IOBuffer()
	  : buf(0), bufused(0), bufsize(0)
	{ }

	IOBuffer(IOBuffer &&b)
	  : buf(b.buf), bufused(b.bufused), bufsize(b.bufsize)
	{
		b.buf = 0;
		b.bufused = 0;
		b.bufsize = 0;
	}

	~IOBuffer() {
		delete[] buf;
	}

	IOBuffer(const IOBuffer &b)
	  : buf(0), bufused(0), bufsize(0)
	{
		*this = b;
	}
	
	IOBuffer& operator=(const IOBuffer &b) {
		if(this!=&b) {
			if(b.bufused>bufsize)
				reserve_extra(b.bufused-bufsize);
			memcpy(buf,b.buf,b.bufused);
			bufused = b.bufused;
		}
		return *this;
	}

	IOBuffer& operator=(IOBuffer &&b) {
		if(this!=&b) {
			delete[] buf;
			buf = b.buf;
			bufused = b.bufused;
			bufsize = b.bufsize;
			b.buf = 0;
			b.bufused = 0;
			b.bufsize = 0;
		}
		return *this;
	}

	void clear() {
		delete[] buf;
		buf = 0;
		bufsize = 0;
		bufused = 0;
	}

	char       *begin()       { return buf; }
	const char *begin() const { return buf; }
	char       *end()       { return buf+bufused; }
	const char *end() const { return buf+bufused; }

	size_t used() const { return bufused; }
	bool empty() const { return bufused==0; }
	size_t spare() const { return bufsize-bufused; }

	void pop_front(size_t bytes) {
		if(bytes==bufused)
			clear();
		else {
			memmove(buf, buf+bytes, bufused-bytes);
			bufused -= bytes;
		}
	}

	void push_back(size_t bytes) {
		bufused += bytes;
	}

	void reserve_extra(size_t extra) {
		if(bufused+extra > bufsize) {
			size_t new_bufsize = (bufused+extra+4095) & ~4095;
			char *new_buf = new char[new_bufsize];
			memcpy(new_buf, buf, bufused);
			delete[] buf;
			buf = new_buf;
			bufsize = new_bufsize;
		}
	}

	void swap(IOBuffer &b) {
		std::swap(buf,b.buf);
		std::swap(bufused,b.bufused);
		std::swap(bufsize,b.bufsize);
	}
};


inline void swap(IOBuffer &a, IOBuffer &b) {
	a.swap(b);
}


#endif
