#include "GbCompress.h"
#include "Mem.h"
#include "Log.h"
#include <string.h>


static void *malloc_replace(void *, unsigned int nitems, unsigned int size) {
	return g_mem.gbmalloc(size*nitems,"zlib");
}

static void free_replace(void *, void *s) {
	g_mem.gbfree(s,"zlib", 0, false);
}


int gbuncompress(unsigned char *dest, uint32_t *destLen,
		 const unsigned char *source, uint32_t sourceLen)
{
	z_stream stream;
	memset(&stream,0,sizeof(stream));
	stream.next_in = (Bytef*)source;
	stream.avail_in = (uInt)sourceLen;
	stream.next_out = dest;
	stream.avail_out = (uInt)*destLen;
	stream.zalloc = malloc_replace;
	stream.zfree  = free_replace;

	//we can be gzip or deflate
	int err = inflateInit2(&stream, 47);

	if(err != Z_OK)
		return err;

	err = inflate(&stream, Z_FINISH);
	if(err != Z_STREAM_END) {
		inflateEnd(&stream);
		if (err == Z_NEED_DICT ||
		    (err == Z_BUF_ERROR && stream.avail_in == 0))
			return Z_DATA_ERROR;
		return err;
	}
	*destLen = stream.total_out;

	err = inflateEnd(&stream);
	return err;
}


int gbcompress(unsigned char *dest, uint32_t *destLen,
	       const unsigned char *source, uint32_t sourceLen)
{

	z_stream stream;
	memset(&stream,0,sizeof(stream));
	stream.next_in = (Bytef*)source;
	stream.avail_in = (uInt)sourceLen;
	stream.next_out = dest;
	stream.avail_out = (uInt)*destLen;
	stream.zalloc = malloc_replace;
	stream.zfree  = free_replace;

	stream.opaque = (voidpf)0;

	//we can be gzip or deflate
	int err = deflateInit (&stream, Z_DEFAULT_COMPRESSION);
	if(err != Z_OK) {
		// zlib's incompatible version error?
		if ( err == -6 ) {
			log("zlib: zlib did you forget to add #pragma pack(4) to "
			    "zlib.h when compiling libz.a so it aligns on 4-byte "
			    "boundaries because we have that pragma in "
			    "gb-include.h so its used when including zlib.h");
		}
		return err;
	}

	err = deflate(&stream, Z_FINISH);

	if(err != Z_STREAM_END) {
		deflateEnd(&stream);
		return err == Z_OK ? Z_BUF_ERROR : err;
	}
	*destLen = stream.total_out;

	err = deflateEnd(&stream);
	return err;
}
