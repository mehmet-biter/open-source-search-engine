#include "gb-include.h"

#include "Mime.h"

// . returns false if could not get a valid mime
// . we need the url in case there's a Location: mime that's base-relative
void Mime::set ( const char *mime , int32_t mimeLen ) {
	m_mime    = mime;
	m_mimeLen = mimeLen;
	m_mimeEnd = mime + mimeLen;
}

// . return ptr to next line to try
// . return NULL if no lines left
const char *Mime::getLine ( const char   *line  ,
		      const char  **field , int32_t *fieldLen ,
		      const char  **value , int32_t *valueLen ) {
	// reset field and value lengths
	*fieldLen = 0;
	*valueLen = 0;
	// a NULL line means the start
	if ( line == NULL ) line = m_mime;
	// a simple ptr
	const char *p    = line;
	const char *pend = m_mimeEnd;
 loop:
	// skip to next field (break on comment)
	while ( p < pend && *p!='#' && !is_alnum_a(*p) ) p++;
	// bail on EOF
	if ( p >= pend ) return NULL;
	// point to next line if comment
	if ( *p == '#' ) {
		while ( p < pend && *p != '\n' && *p !='\r' ) p++;
		// NULL on EOF
		if ( p >= pend ) return NULL;
		// try to get another line
		goto loop;
	}
	// save p's position
	const char *s = p;
	// continue until : or \n or \r or EOF
	while ( p < pend && *p != ':' && *p != '\n' && *p !='\r' ) p++;
	// NULL on EOF
	if ( p >= pend ) return NULL;
	// return p for getting next line if no : found
	if ( *p != ':' ) goto loop;
	// set the field
	*field    = s;
	// set the field length
	*fieldLen = p - s;
	// reset value length to 0, in case we find none
	*valueLen = 0;
	// skip over :
	p++;
	// skip normal spaces and tabs at p now
	while ( p < pend && (*p==' ' || *p=='\t') ) p++;
	// NULL on EOF
	if ( p >= pend ) return NULL;
	// value is next
	*value = p;
	// goes till we hit \r or \n
	while ( p < pend && *p != '\n' && *p !='\r' ) p++;
	// set value length
	*valueLen = p - *value;
	// NULL on EOF
	if ( p >= pend ) return NULL;
	// otherwise, p is start of next line
	return p;
}
