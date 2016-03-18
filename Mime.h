// Matt Wells, copyright Jun 2000

// . class to parse a standard MIME file

#ifndef GB_MIME_H
#define GB_MIME_H

#include <time.h>   // time_t mktime()
#include "Url.h"

class Mime {

 public:

	// just sets m_mime/m_mimeLen
	void set ( const char *mime , int32_t mimeLen );

	// . returns a ptr to next line
	// . fills in your "field/value" pair of this line
	// . skips empty and comment lines automatically
	const char *getLine ( const char   *line  ,
			const char  **field , int32_t *fieldLen ,
			const char  **value , int32_t *valueLen ) ;

	// use this to get the value of a unique field
	const char *getValue ( const char *field , int32_t *valueLen );

 private:

	const char *m_mime;
	int32_t  m_mimeLen;
	const char *m_mimeEnd;
};

#endif // GB_MIME_H
