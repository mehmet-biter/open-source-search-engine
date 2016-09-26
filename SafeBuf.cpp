#include "SafeBuf.h"
#include "gb-include.h"
#include "Mem.h"
#include "Conf.h"
#include "Words.h"
#include "Sanity.h"
#include <sys/stat.h> //O_CREAT etc.
#include <fcntl.h>    //open()


SafeBuf::SafeBuf(int32_t initSize, const char *label ) {
	if(initSize <= 0) initSize = 1;
	m_capacity = initSize;
	m_length = 0;
	m_label = label;
	m_buf = (char*)mrealloc(NULL, 0, m_capacity, m_label );
	if(!m_buf) m_capacity = 0;
	m_usingStack = false;
	m_encoding = csUTF8;
}

void SafeBuf::constructor ( ) {
	m_capacity = 0;
	m_length = 0;
	m_buf = NULL;
	m_usingStack = false;
	m_encoding = csUTF8;
	m_label = NULL;
}	

SafeBuf::SafeBuf() {
	m_capacity = 0;
	m_length = 0;
	m_buf = NULL;
	m_usingStack = false;
	m_encoding = csUTF8;
	m_label = NULL;
}

void SafeBuf::setLabel ( const char *label ) {
	m_label = label;
}

SafeBuf::SafeBuf(char* stackBuf, int32_t cap, const char* label) {
	m_usingStack = true;
	m_capacity = cap;
	m_buf = stackBuf;
	m_length = 0;
	m_encoding = csUTF8;
	m_label = label;
}

SafeBuf::SafeBuf(char *heapBuf, int32_t bufMax, int32_t bytesInUse, bool ownData) {
	// . If we don't own the data, treat it like a stack buffer
	//   so we won't attempt to free or realloc it.
	// . If you already have data in this buffer, make sure
	//   to explicitly update length.
	m_usingStack = !ownData;
	m_capacity = bufMax;
	m_buf = heapBuf;
	m_length = bytesInUse;
	m_encoding = csUTF8;
}

SafeBuf::~SafeBuf() {
	if(!m_usingStack && m_buf)
		mfree(m_buf, m_capacity, "SafeBuf");
	m_buf = NULL;
}

bool SafeBuf::setBuf(char *newBuf, int32_t bufMax, int32_t bytesInUse, bool ownData,
		     int16_t encoding ){
	// . Passing in a null or a capacity smaller than the
	//   used portion of the buffer is pointless, we have
	//   more reliable functions for emptying a buffer.
	if ( !newBuf || bufMax < bytesInUse || bytesInUse < 0 )
		return false;
	if ( !m_usingStack && m_buf )
		mfree( m_buf, m_capacity, "SafeBuf" );
	// . If we don't own the data, treat it like a stack buffer
	//   so we won't attempt to free or realloc it.
	m_usingStack = !ownData;
	m_buf = newBuf;
	m_capacity = bufMax;
	m_length = bytesInUse;
	m_encoding = csUTF8;
	if ( encoding > 0 ) m_encoding = encoding;
	return true;
}

void SafeBuf::purge() {
	if ( !m_usingStack && m_buf )
		mfree( m_buf, m_capacity, "SafeBuf" );
	m_buf = NULL;
	m_capacity = 0;
	m_length = 0;
	m_usingStack = false;
	m_encoding = csUTF8;
}

bool SafeBuf::safePrintf(const char *formatString , ...) {
	va_list   ap;
	va_start ( ap, formatString);
	int32_t tmp = vsnprintf ( m_buf + m_length, m_capacity - m_length, 
			       formatString , ap );
	va_end(ap);
	if(tmp + m_length +1>= m_capacity) {
		// +1 some space for a silent \0 at the end
		if(!reserve(m_capacity + tmp + 1)) return false;

		va_start ( ap, formatString);
		tmp = vsnprintf ( m_buf + m_length, m_capacity - m_length,
				  formatString , ap );
		va_end(ap);
	}
	m_length += tmp;
	// this should not hurt anything
	m_buf[m_length] = '\0';
	return true;
}


bool SafeBuf::safeMemcpy(const char *s, int32_t len) {
	// put a silent \0 at the end
	//int32_t tmp = len + m_length+1;
	//if(tmp >= m_capacity ) {
	if ( m_length + len > m_capacity ) {
		if ( ! reserve(m_length+len) ) return false;
	}
	gbmemcpy(m_buf + m_length, s, len);
	m_length = m_length + len; // tmp-1;
	// this should not hurt anything
	//m_buf[m_length] = '\0';
	return true;
}

bool SafeBuf::safeMemcpy_nospaces(const char *s, int32_t len) {
	// put a silent \0 at the end
	int32_t tmp = len + m_length+1;
	if(tmp >= m_capacity ) {
		if ( ! reserve(tmp) ) return false;
	}
	for ( int32_t i = 0 ; i < len ; i++ ) {
		if ( is_wspace_a(s[i]) ) continue;
		m_buf[m_length++] = s[i];
	}
	//gbmemcpy(m_buf + m_length, s, len);
	//m_length = tmp-1;
	// this should not hurt anything
	m_buf[m_length] = '\0';
	return true;
}


bool SafeBuf::pushLong ( int32_t i) {
	if ( m_length + 4 > m_capacity ) 
		if(!reserve(4))//2*m_capacity + 1))
			return false;
	*(int32_t *)(m_buf+m_length) = i;
	m_length += 4;
	return true;
}

bool SafeBuf::pushLongLong ( int64_t i) {
	if ( m_length + 8 > m_capacity && ! reserve(8) )
		return false;
	*(int64_t *)(m_buf+m_length) = i;
	m_length += 8;
	return true;
}

bool SafeBuf::pushFloat ( float i) {
	if ( m_length + 4 > m_capacity ) 
		if(!reserve(4))
			return false;
	*(float *)(m_buf+m_length) = i;
	m_length += 4;
	return true;
}

int32_t SafeBuf::popLong ( ) {
	if ( m_length < 4 ) gbshutdownLogicError();
	int32_t ret = *(int32_t *)(m_buf+m_length-4);
	m_length -= 4;
	return ret;
}

float SafeBuf::popFloat ( ) {
	if ( m_length < 4 ) gbshutdownLogicError();
	float ret = *(float *)(m_buf+m_length-4);
	m_length -= 4;
	return ret;
}

int32_t SafeBuf::pad(const char ch, const int32_t len) {
	for(int32_t i = 0; i < len; ++i)
		pushChar(ch);
	return len;
}

bool SafeBuf::cat(const SafeBuf& c) {
	return safeMemcpy(c.getBufStart(), c.length());
}

bool SafeBuf::reserve(int32_t i , const char *label, bool clearIt ) {

	// if we don't already have a label and they provided one, use it
	if ( ! m_label ) {
		if ( label ) m_label = label;
		else         m_label = "SafeBuf";
	}

	if(m_length + i > m_capacity) {
		char *tmpBuf = m_buf;
		int32_t tmpCap = m_capacity;
		if(m_usingStack) {
			m_buf = NULL;
			m_capacity += i;
			//if(m_capacity < 8) m_capacity = 8;
			m_buf = (char*)mrealloc(m_buf, 0, m_capacity,m_label);
			if(!m_buf) {
				m_buf = tmpBuf;
				log("safebuf: failed to reserve %" PRId32" bytes",
				    m_capacity);
				m_capacity = tmpCap;
				return false;
			}
			log(LOG_DEBUG, "query: safebuf switching to heap: %" PRId32,
			    m_capacity);
			gbmemcpy(m_buf, tmpBuf, m_length);
			// reset to 0's?
			if ( clearIt ) {
				int32_t clearSize = m_capacity - tmpCap;
				memset(m_buf+tmpCap,0,clearSize);
			}
			m_usingStack = false;
			return true;
		}
		m_capacity += i;
		//if(m_capacity < 8) m_capacity = 8;
		m_buf = (char*)mrealloc(m_buf, tmpCap, m_capacity,m_label);
		if(!m_buf) {
			m_buf = tmpBuf;
			log("safebuf: failed to realloc %" PRId32" bytes",m_capacity);
			m_capacity = tmpCap;
			return false;
		}
		// reset to 0's?
		if ( clearIt ) {
			int32_t clearSize = m_capacity - tmpCap;
			memset(m_buf+tmpCap,0,clearSize);
		}
		// log(LOG_DEBUG, "query: resize safebuf %" PRId32" to %" PRId32, tmpCap, m_capacity);
	}
	// reset to 0's?
	//if ( ! clearIt ) return true;
	//int32_t clearSize = m_capacity - m_length;
	//memset(m_buf+m_length,0,clearSize);
	return true;
}


//reserve this many bytes, if we need to alloc, we double the 
//buffer size.
bool SafeBuf::reserve2x(int32_t i, const char *label) {
	//watch out for overflow!
	if((m_capacity << 1) + i < m_capacity) return false;
	if(i + m_length >= m_capacity)
		return reserve(m_capacity + i,label);
	else return true;
}

int32_t SafeBuf::saveToFile ( const char *dir, const char *filename ) {
	char buf[1024];
	snprintf(buf,1024,"%s/%s",dir,filename);
	return dumpToFile ( buf );
}

int32_t SafeBuf::save ( const char *fullFilename ) {
	return dumpToFile ( fullFilename );
}

int32_t SafeBuf::dumpToFile(const char *filename ) {
	int32_t fd = open ( filename , O_CREAT | O_WRONLY | O_TRUNC ,
			    getFileCreationFlags() );
			    //S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH );
	if ( fd < 0 ) {
		log("safebuf: Failed to open %s for writing: %s", 
		    filename,mstrerror(errno));
		return -1;
	}
	//logf(LOG_DEBUG, "test: safebuf %" PRId32" bytes written to %s",m_length,
	//     filename);
	int32_t bytes = write(fd, (char*)m_buf, m_length) ;
	if ( bytes != m_length ) {
		logf(LOG_DEBUG,"test: safebuf bad write %" PRId32" != %" PRId32": %s",
		     bytes,m_length,mstrerror(errno));
		close(fd);
		return -1;
	}
	close(fd);
	return m_length;
}

// return -1 on error
int32_t SafeBuf::safeSave (char *filename ) {

	// first write to tmp file
	char tmp[1024];
	SafeBuf fn(tmp,1024);
	fn.safePrintf( "%s.saving",filename );

	int32_t fd = open ( fn.getBufStart() ,
			    O_CREAT | O_WRONLY | O_TRUNC ,
			    getFileCreationFlags() );
			 // S_IRUSR |S_IWUSR |S_IRGRP |S_IWGRP| S_IROTH );
	if ( fd < 0 ) {
		log("safebuf: Failed to open %s for writing: %s", 
		    fn.getBufStart(), mstrerror(errno));
		return -1;
	}
	//logf(LOG_DEBUG, "test: safebuf %" PRId32" bytes written to %s",m_length,
	//     filename);
	int32_t bytes = write(fd, (char*)m_buf, m_length) ;
	if ( bytes != m_length ) {
		logf(LOG_DEBUG,"test: safebuf bad write %" PRId32" != %" PRId32": %s",
		     bytes,m_length,mstrerror(errno));
		close(fd);
		return -1;
	}
	close(fd);

	// remove original file before replacing it
	(void)::unlink(filename);

	// now move it to the actual filename
	//int32_t status = ::rename ( fn.getBufStart() , filename );
	int32_t status = ::link ( fn.getBufStart() , filename );

	// note it
	//log("sb: saved %s safely",filename);

	if ( status == -1 ) {
		//g_errno = errno;
		log("sb: safeBuf::safeSave::link: %s",mstrerror(errno));
		return -1;
	}

	return m_length;
}


int32_t SafeBuf::fillFromFile(const char *dir, const char *filename, const char *label) {
	m_label = label;
	char buf[1024];
	if ( dir ) snprintf(buf,1024,"%s/%s",dir,filename);
	else       snprintf(buf,1024,"%s",filename);
	return fillFromFile ( buf );
}

char *SafeBuf::getNextLine ( char *p ) {
	// skip till \n
	for ( ; *p ; p++ )
		if ( *p == '\n' ) break;
	if ( *p == '\n' ) p++;
	if ( *p == '\0' ) return NULL;
	return p;
}

// returns -1 on error
int32_t SafeBuf::catFile(const char *filename) {
	SafeBuf sb2;
	if ( sb2.fillFromFile(filename) < 0 ) return -1;
	// add 1 for a null
	if ( ! reserve ( sb2.length() + 1 ) ) return -1;
	cat ( sb2 );
	return length();
}


// returns -1 on error
int32_t SafeBuf::fillFromFile(const char *filename) {
	int32_t fd = open(filename, O_RDONLY);
	if(fd<0) {
		log(LOG_DEBUG, "query: Failed to open %s for reading: ",
		    filename);
		if(errno==ENOENT)
			return 0;// 0 means does not exist or file is empty
		// -1 means there was a read error of some sorts
		return -1;//false;
	}

	struct stat st;
	if(fstat(fd, &st) != 0) {
		// An error occurred
		log(LOG_DEBUG, "query: Failed to open %s for reading: ", 
		    filename);
		close(fd);
		return 0;
	}
	reserve(st.st_size+1);
	
	int32_t numRead = read(fd, m_buf+m_length, st.st_size);
	close(fd);
	// add a \0 for good meaure
	if ( numRead >= 0 ) {
		m_length += numRead;
		m_buf[m_length] = '\0';
	}

	return numRead;
}

bool SafeBuf::insert ( const char *s, int32_t insertPos ) {
	return safeReplace ( s         ,
			     strlen(s) ,
			     insertPos ,
			     0         );
}

bool SafeBuf::insert2 ( const char *s, int32_t slen, int32_t insertPos ) {
	return safeReplace ( s         ,
			     slen      ,
			     insertPos ,
			     0         );
}

bool SafeBuf::replace ( const char *src, const char *dst ) {
	int32_t len1 = strlen(src);
	int32_t len2 = strlen(dst);
	if ( len1 != len2 ) {
		return safeReplace2 ( src , len1,
				      dst , len2,
				      0 );
	}
	for ( char *p = strstr ( m_buf , src ) ; p ; p = strstr(p+len1,src ) )
		gbmemcpy ( p , dst , len2 );
	return true;
}

bool SafeBuf::removeChunk1 ( char *p , int32_t len ) {
	int32_t off = p - m_buf;
	return removeChunk2 ( off , len );
}

bool SafeBuf::removeChunk2 ( int32_t pos , int32_t len ) {
	if ( len == 0 ) return true;
	char *dst = m_buf + pos;
	char *src = m_buf + pos + len;
	int32_t moveLen = m_buf + m_length - src;
	memmove(dst, src, moveLen);
	m_length -= len;
	m_buf[m_length] = '\0';
	return true;
}


// replace string at "pos/replaceLen" with s/len
bool SafeBuf::safeReplace ( const char *s, int32_t len, int32_t pos, int32_t replaceLen ) {
	// make sure we have room
	int32_t diff = len - replaceLen;
	// add in one for the silent terminating \0
	int32_t newLen = m_length + diff ;
	if ( newLen+1 > m_capacity ) {
		if ( !reserve ( 2 * newLen + 1 ) )
			return false;
	}
	// shift memory over
	if ( diff != 0 ) {
		char *src = m_buf + (pos + replaceLen);
		char *dst = src + diff;
		int32_t  movelen = m_length - (pos + replaceLen);
		memmove(dst, src, movelen);
	}
	// replace
	char *p = m_buf + pos;
	gbmemcpy(p, s, len);
	m_length = newLen;
	// silent terminating \0
	m_buf[m_length] = '\0';
	return true;
}

// return false and set g_errno on error
bool SafeBuf::safeReplace2 (const char *s, int32_t slen,
			    const char *t, int32_t tlen,
			     int32_t startOff ) {

	char *pend2 = m_buf + m_length - slen + 1;
	int32_t count = 0;
	for ( char *p = m_buf + startOff ; p < pend2 ; p++ ) {
		// search
		if ( p[0] != s[0] ) continue;
		// compare 2nd char
		if ( slen >= 2 && p[1] != s[1] ) continue;
		// check all chars now
		if ( slen >= 3 && strncmp(p,s,slen) ) continue;
		// count them
		count++;
	}

	// nothing to replace? all done then
	if ( count == 0 ) return true;
	
	int32_t extra = (tlen - slen) * count;
	// allocate new space
	int32_t need = m_length + extra;
	// make a new safebuf to copy into
	char *bbb = (char *)mmalloc(need,"saferplc");
	if ( ! bbb ) return false;
	// do it
	char *dst = bbb;
	char *pend = m_buf + m_length;
	// scan all
	for ( char *p = m_buf ; p < pend ; p++ , dst++ ) {
		// assume not a match
		*dst = *p;
		// search
		if ( p[0] != s[0] ) continue;
		// must be big enough
		if ( p + slen > pend ) continue;
		// compare 2nd char
		if ( slen >= 2 && p[1] != s[1] ) continue;
		// check all chars now
		if ( slen >= 3 && strncmp(p,s,slen) ) continue;
		// undo copy
		gbmemcpy ( dst , t , tlen );
		// advance for that
		dst += tlen - 1;
		p   += slen - 1;
	}
	// clear us
	purge();
	// now this is our new junk
	bool status = safeMemcpy ( bbb , dst - bbb );
	// clear what we had
	mfree ( bbb , need , "saferplc");
	return status;
}

bool  SafeBuf::utf8Encode2(char *s, int32_t len, bool encodeHTML) {
	int32_t tmp = m_length;
	if ( m_encoding == csUTF8 ) {
		if (! safeMemcpy(s,len)) return false;
	} else {
		return false;
	}
	if (!encodeHTML) return true;
	return htmlEncode(m_length-tmp);
}



bool SafeBuf::utf32Encode(UChar32* codePoints, int32_t cpLen) {
	if(m_encoding != csUTF8) return safePrintf("FIXME %s:%i", __FILE__, __LINE__);

    int32_t need = 0;
    for(int32_t i = 0; i < cpLen;i++) need += utf8Size(codePoints[i]);
	if(!reserve(need)) return false;
    
    for(int32_t i = 0; i < cpLen;i++) {
		m_length += ::utf8Encode(codePoints[i], m_buf + m_length);
	}
	
    return true;
}

bool SafeBuf::cdataEncode ( const char *s ) {
	return safeCdataMemcpy(s,strlen(s));
}

bool SafeBuf::cdataEncode ( const char *s , int32_t len ) {
	return safeCdataMemcpy(s,len);
}


bool  SafeBuf::safeCdataMemcpy ( const char *s, int32_t len ) {
	int32_t len1 = m_length;
	if ( !safeMemcpy(s,len) )
		return false;
	// check the written section for bad characters
	int32_t p = len1;
	while ( p < m_length-2 ) {
		if ( m_buf[p]==']' && m_buf[p+1]==']' && m_buf[p+2]=='>') {
			// rewrite the > as &gt
			safeReplace("&gt", 3, p+2, 1);
		}
		p++;
	}
	return true;
}

bool  SafeBuf::htmlEncode(const char *s, int32_t lenArg, bool encodePoundSign , int32_t truncateLen ) {
	// . we assume we are encoding into utf8
	// . sanity check
	if ( m_encoding == csUTF16 ) gbshutdownLogicError();

	// the new truncation logic
	int32_t len = lenArg;
	bool truncate = false;
	int32_t extra = 0;
	if ( truncateLen > 0 && lenArg > truncateLen ) {
		len = truncateLen;
		truncate = true;
		extra = 4;
	}

	// alloc some space if we need to. add a byte for NULL termination.
	if ( m_length + len + 1 + extra >= m_capacity && !reserve( m_capacity + len + 1 + extra ) ) {
		return false;
	}

	// tmp vars
	char *t    = m_buf + m_length;
	char *tend = m_buf + m_capacity;
	// scan through all 
	const char *send = s + len;

	for ( ; s < send ; s++ ) {
		// ensure we have enough room
		if ( t + 12 >= tend ) {
			// save progress
			int32_t written = t - m_buf;
			if ( !reserve( m_capacity + 100 ) ) {
				return false;
			}

			// these might have changed, so set them again
			t    = m_buf + written;
			tend = m_buf + m_capacity;
		}
		// this is only used to encode stuff for the validation
		// routine (see XmlDoc::validateOutput). like storing the
		// event tile or description ultimately unto a checkboxspan.*
		// txt file, so don't mess with these punct chars either

		// convert it?
		if ( *s == '"' ) {
			*t++ = '&';
			*t++ = '#';
			*t++ = '3';
			*t++ = '4';
			*t++ = ';';
			continue;
		}
		if ( *s == '<' ) {
			*t++ = '&';
			*t++ = 'l';
			*t++ = 't';
			*t++ = ';';
			continue;
		}
		if ( *s == '>' ) {
			*t++ = '&';
			*t++ = 'g';
			*t++ = 't';
			*t++ = ';';
			continue;
		}
		if ( *s == '&' ) {
			*t++ = '&';
			*t++ = 'a';
			*t++ = 'm';
			*t++ = 'p';
			*t++ = ';';
			continue;
		}
		if ( *s == '#' && encodePoundSign ) {
			*t++ = '&';
			*t++ = '#';
			*t++ = '0';
			*t++ = '3';
			*t++ = '5';
			*t++ = ';';
			continue;
		}

		*t++ = *s;		
	}

	if ( truncate ) {
		gbmemcpy ( t , "... " , 4 );
		t += 4;
	}

	*t = '\0';

	// update the used buf length
	m_length = t - m_buf ;

	// success
	return true;
}

bool SafeBuf::htmlEncode(const char *s) {
	return htmlEncode(s,strlen(s),true);
}

// scan the last "len" characters for entities to encode
bool SafeBuf::htmlEncode(int32_t len ){
	for (int32_t i = m_length-len; i < m_length ; i++){

		if ( m_buf[i] == '"' ) {
			if (!safeReplace("&#34;", 4, i, 1))
				return false;
			continue;
		}
		if ( m_buf[i] == '<' ) {
			if (!safeReplace("&lt;", 4, i, 1))
				return false;
			continue;
		}
		if ( m_buf[i] == '>' ) {
			if (!safeReplace("&gt;", 4, i, 1))
				return false;
			continue;
		}
		if ( m_buf[i] == '&' ) {
			if (!safeReplace("&amp;", 5, i, 1))
				return false;
			continue;
		}
	}
	return true;
}

// a static buffer for speed
static char s_ut[256];
static bool s_init23 = false;

void initTable ( ) {
	if ( s_init23 ) return;
	s_init23 = true;
	for ( int32_t c = 0 ; c <= 255 ; c++ ) {
		// assume we must encode it
		s_ut[c] = 1;
		if ( ! is_ascii ( (unsigned char)c ) ) continue;
		if ( c == ' ' ) continue;
		if ( c == '&' ) continue;
		if ( c == '"' ) continue;
		if ( c == '+' ) continue;
		if ( c == '%' ) continue;
		if ( c == '#' ) continue;
		if ( c == '<' ) continue;
		if ( c == '>' ) continue;
		if ( c == '?' ) continue;
		if ( c == ':' ) continue;
		if ( c == '/' ) continue;
		// no need to encode it!
		s_ut[c] = 0;
	}
}

//s is a url, make it safe for printing to html
bool SafeBuf::urlEncode (const char *s , int32_t slen,
			 bool requestPath ,
			 bool encodeApostrophes ) {
	// this makes things faster
	if ( ! s_init23 ) initTable();

	const char *send = s + slen;
	for ( ; s < send ; s++ ) {
		if ( *s == '\0' && requestPath ) {
			pushChar(*s);
			continue;
		}
		if ( *s == '\'' && encodeApostrophes ) {
			safeMemcpy("%27",3);
			continue;
		}

		// skip if no encoding required
		if ( s_ut[(unsigned char)*s] == 0 ) {
			pushChar(*s); 
			continue; 
		}
		// special case
		if ( *s == '?' && requestPath ) {
			pushChar(*s); 
			continue; 
		}

		// space to +
		if ( *s == ' ' ) { pushChar('+'); continue; }
		pushChar('%');
		// store first hex digit
		unsigned char v = ((unsigned char)*s)/16 ;
		if ( v < 10 ) v += '0';
		else          v += 'A' - 10;
		pushChar(v);
		// store second hex digit
		v = ((unsigned char)*s) & 0x0f ;
		if ( v < 10 ) v += '0';
		else          v += 'A' - 10;
		pushChar(v);
	}
	nullTerm();
	return true;
}



bool SafeBuf::dequote ( const char *t , int32_t tlen ) {
	if ( tlen == 0 ) return true;
	const char *tend = t + tlen;
	for ( ; t < tend; t++ ) {
		if ( *t == '"' ) {
			safeMemcpy("&#34;", 5);
			continue;
		}
		safeMemcpy(t, 1);
	}
	// there may be more things added to m_buf later, so do
	// not null terminate this here...
	//char null = '\0';
	//safeMemcpy(&null, 1);
	return true;
}


void SafeBuf::detachBuf() {
	m_capacity = 0;
	m_length = 0;
	m_buf = NULL;
	m_usingStack = false;
}

// set buffer from another safebuf, stealing it
bool SafeBuf::stealBuf ( SafeBuf *sb ) {
	// free what we got!
	purge();
	m_capacity   = sb->m_capacity;
	m_length     = sb->m_length;
	m_buf        = sb->m_buf;
	m_usingStack = sb->m_usingStack;
	m_encoding   = sb->m_encoding;
	// clear his ptrs
	sb->m_buf = NULL;
	sb->m_capacity = 0;
	sb->m_length = 0;
	return true;
}


bool SafeBuf::operator += (uint64_t i) {
	return safeMemcpy((char*)&i, sizeof(uint64_t));
}

bool SafeBuf::operator += (int64_t i) {
	return safeMemcpy((char*)&i, sizeof(int64_t));
}

bool SafeBuf::operator += (char c) {
	return safeMemcpy((char*)&c, sizeof(char));
}

bool SafeBuf::operator += (uint32_t i) {
	return safeMemcpy((char*)&i, sizeof(uint32_t));
}

bool SafeBuf::operator += (uint16_t i) {
	return safeMemcpy((char*)&i, sizeof(uint16_t));
}

bool SafeBuf::operator += (uint8_t i) {
	return safeMemcpy((char*)&i, sizeof(uint8_t));
}


// bool SafeBuf::operator += (double i) {
// 	return safeMemcpy((char*)&i, sizeof(double));
// }

char& SafeBuf::operator[](int32_t i) {
	return m_buf[i];
}

#include "Tagdb.h"

// if safebuf is a buffer of Tags from Tagdb.cpp
Tag *SafeBuf::addTag2 ( const char *mysite ,
			const char *tagname ,
			int32_t  now ,
			const char *user ,
			int32_t  ip ,
			int32_t  val ,
			rdbid_t rdbId) {
	char buf[64];
	sprintf(buf,"%" PRId32,val);
	int32_t dsize = strlen(buf) + 1;
	return addTag ( mysite,tagname,now,user,ip,buf,dsize,rdbId,true);
}

// if safebuf is a buffer of Tags from Tagdb.cpp
Tag *SafeBuf::addTag3 ( const char *mysite ,
			const char *tagname ,
			int32_t  now ,
			const char *user ,
			int32_t  ip ,
			const char *data ,
			rdbid_t rdbId) {
	int32_t dsize = strlen(data) + 1;
	return addTag ( mysite,tagname,now,user,ip,data,dsize,rdbId,true);
}

Tag *SafeBuf::addTag ( const char *mysite ,
		       const char *tagname ,
		       int32_t  now ,
		       const char *user ,
		       int32_t  ip ,
		       const char *data ,
		       int32_t  dsize ,
		       rdbid_t rdbId,
		       bool  pushRdbId ) {
	int32_t need = dsize + 32 + sizeof(Tag);
	if ( user   ) need += strlen(user);
	if ( mysite ) need += strlen(mysite);
	if ( ! reserve ( need ) ) return NULL;
	if ( pushRdbId && ! pushChar(rdbId) ) return NULL;
	Tag *tag = (Tag *)getBuf();
	tag->set(mysite,tagname,now,user,ip,data,dsize);
	incrementLength ( tag->getRecSize() );
	if ( tag->getRecSize() > need ) gbshutdownLogicError();
	return tag;
}

bool SafeBuf::addTag ( Tag *tag ) {
	int32_t recSize = tag->getSize();
	//tag->setDataSize();
	if ( tag->m_recDataSize <= 16 ) { 
		// note it
		log(LOG_WARN, "safebuf: encountered corrupted tag datasize=%" PRId32".", tag->m_recDataSize);
		return false;
		//g_process.shutdownAbort(true); }
	}
	return safeMemcpy ( (char *)tag , recSize );
}

// this puts a \0 at the end but does not update m_length for the \0 
bool  SafeBuf::safeStrcpy ( const char *s ) {
	if ( ! s ) return true;
	int32_t slen = strlen(s);
	if ( ! reserve ( slen+1 ) ) return false;
	if ( ! safeMemcpy(s,slen) ) return false;
	nullTerm();
	return true;
}

// . this should support the case when the src and dst buffers are the same!
//   so decodeJSONToUtf8() function below will work
// . this is used by xmldoc.cpp to PARTIALLY decode a json buf so we do not
//   index letters in escapes like \n \r \f \t \uxxxx \\ \/
// . SO we do keep \" 
// . so when indexing a doc we set decodeAll to FALSE, but if you want to 
//   decode quotation marks as well then set decodeAll to TRUE!
bool SafeBuf::safeDecodeJSONToUtf8 ( const char *json, int32_t jsonLen) {

	// how much space to reserve for the copy?
	int32_t need = jsonLen;

	// count how many \u's we got
	const char *p = json;//m_buf;
	const char *pend = json + jsonLen;
	for ( ; p < pend ; p++ ) 
		// for the 'x' and the ';'
		if ( *p == '\\' && p[1] == 'u' ) need += 2;

	// reserve a little extra if we need it
	//SafeBuf dbuf;
	if ( ! reserve ( need + 1) ) return false;

	const char *src = json;//m_buf;
	const char *srcEnd = json + jsonLen;

	char *dst = m_buf + m_length;

	for ( ; src < srcEnd ; ) {
		if ( *src == '\\' ) {
			// \n? (from json.org homepage)
			if ( src[1] == 'n' ) {
				*dst++ = '\n';
				src += 2;
				continue;
			}
			if ( src[1] == 'r' ) {
				*dst++ = '\r';
				src += 2;
				continue;
			}
			if ( src[1] == 't' ) {
				*dst++ = '\t';
				src += 2;
				continue;
			}
			if ( src[1] == 'b' ) {
				*dst++ = '\b';
				src += 2;
				continue;
			}
			if ( src[1] == 'f' ) {
				*dst++ = '\f';
				src += 2;
				continue;
			}
			// a "\\" is an encoded backslash
			if ( src[1] == '\\' ) {
				*dst++ = '\\';
				src += 2;
				continue;
			}
			// a "\/" is an encoded forward slash
			if ( src[1] == '/' ) {
				*dst++ = '/';
				src += 2;
				continue;
			}
			// we do not decode quotation marks when indexing
			// the doc so we can preserve json names/value pair
			// information for indexing purposes. however,
			// Title.cpp DOES want to decode quotations.
			if ( src[1] == '\"' ) { // && decodeAll ) {
				*dst++ = '\"';
				src += 2;
				continue;
			}
			// utf8? if not, just skip the slash
			if ( src[1] != 'u'  ) { 
				// no, keep the slash so if we have /"
				// we do not convert to just "
				*dst++ = '\\';
				src++; 
				continue; 
			}
			// otherwise, decode. can do in place like this...
			const char *p = src + 2;
			// skip the /ug or /ugg or /uggg or /ugggg in its
			// entirety i guess... to avoid infinite loop
			if ( ! is_hex(p[0]) ) { src +=2; continue;}
			if ( ! is_hex(p[1]) ) { src +=3; continue;}
			if ( ! is_hex(p[2]) ) { src +=4; continue;}
			if ( ! is_hex(p[3]) ) { src +=5; continue;}
			// TODO: support surrogate pairs in utf16?
			UChar32 uc = 0;
			// store the 16-bit number in lower 16 bits of uc...
			hexToBin ( p   , 2 , ((char *)&uc)+1 );
			hexToBin ( p+2 , 2 , ((char *)&uc)+0 );
			//buf[2] = '\0';
			int32_t size = ::utf8Encode ( (UChar32)uc , (char *)dst );
			// a quote??? not allowed in json!
			if ( size == 1 && dst[0] == '\"' ) {
				size = 2;
				dst[0] = '\\';
				dst[1] = '\"';
			}
			//int16_t = ahextoint16_t ( p );
			dst += size;
			// skip over /u and 4 digits
			src += 6;
			continue;
		}
		*dst++ = *src++;
	}
	*dst = '\0';
	// update our length
	m_length = dst - m_buf;

	return true;
}

bool SafeBuf::jsonEncode ( char *src , int32_t srcLen ) {
	char c = src[srcLen];
	src[srcLen] = 0;
	bool status = jsonEncode ( src );
	src[srcLen] = c;
	return status;
}

// encode into json
bool SafeBuf::safeUtf8ToJSON ( const char *utf8 ) {
	if ( ! utf8 ) {
		return true;
	}

	// how much space do we need?
	// each single byte \t char for instance will need 2 bytes
	int32_t need = strlen(utf8) * 2 + 1;
	if ( ! reserve ( need ) ) {
		return false;
	}

	// scan and copy
	const char *src = utf8;

	// concatenate to what's already there
	char *dst = m_buf + m_length;
	char size;
	for ( ; *src ; src += size ) {
		size = getUtf8CharSize(src);

		// remove invalid UTF-8 characters
		if ( ! isValidUtf8Char(src) ) {
			size = 1;
			continue;
		}

		if ( size == 1 ) {
			// remove invalid ascii control characters
			if ((*src >= 1 && *src <= 7) ||
			    (*src == 11) ||
			    (*src >= 14 && *src <= 31)) {
				continue;
			}

			// refer to: http://json.org/
			// backspace (08)
			if ( *src == '\b' ) {
				*dst++ = '\\';
				*dst++ = 'b';
				continue;
			}

			// horizontal tab (09)
			if ( *src == '\t' ) {
				*dst++ = '\\';
				*dst++ = 't';
				continue;
			}

			// newline (10)
			if ( *src == '\n' ) {
				*dst++ = '\\';
				*dst++ = 'n';
				continue;
			}

			// formfeed (12)
			if ( *src == '\f' ) {
				*dst++ = '\\';
				*dst++ = 'f';
				continue;
			}

			// carriage return (13)
			if ( *src == '\r' ) {
				*dst++ = '\\';
				*dst++ = 'r';
				continue;
			}

			// quotation mark
			if ( *src == '\"' ) {
				*dst++ = '\\';
				*dst++ = '\"';
				continue;
			}

			// reverse solidus
			if ( *src == '\\' ) {
				*dst++ = '\\';
				*dst++ = '\\';
				continue;
			}

			*dst++ = *src;
			continue;
		}

		for (int i = 0; i < size; ++i) {
			*dst++ = src[i];
		}
	}

	// null term
	*dst = '\0';

	m_length = dst - m_buf;

	return true;
}


bool SafeBuf::brify2 ( const char *s, int32_t cols, const char *sep, bool isHtml ) {
	return brify ( s, strlen(s), cols , sep , isHtml );
}

bool SafeBuf::brify( const char *s, int32_t slen, int32_t maxCharsPerLine, const char *sep,
					 bool isHtml ) {
	// count the xml tags so we know how much buf to allocated
	const char *p = s;
	const char *pend = s + slen;
	char cs;
	int32_t brSizes = 0;
	bool lastRound = false;
	int32_t col = 0;
	const char *pstart = s;
	const char *breakPoint = NULL;
	bool inTag = false;
	int32_t sepLen = strlen(sep);
	bool forced = false;

redo:

	for ( ; p < pend ; p += cs ) {
		cs = getUtf8CharSize(p);

		// do not inc count if in a tag
		if ( inTag ) {
			if ( *p == '>' ) {
				inTag = false;
			}
			continue;
		}

		if ( *p == '<' && isHtml ) {
			inTag = true;
			continue;
		}

		col++;
		if ( is_wspace_utf8(p) ) {
			// reset?
			if ( ! isHtml && *p == '\n' ) {
				forced = true;
				breakPoint = p;
				goto forceBreak;
			}

			// break AFTER this punct
			breakPoint = p;
			continue;
		}

		if ( col < maxCharsPerLine ) {
			continue;
		}

	forceBreak:
		// now add the break point i guess
		// if none, gotta break here for sure!!!
		if ( ! breakPoint ) {
			breakPoint = p;
		}

		// count that
		brSizes += sepLen;//4;

		// print only for last round
		if ( lastRound ) {
			// . print up to that
			// . this includes the \n if forced is true
			safeMemcpy ( pstart , breakPoint - pstart + 1 );

			// then br
			if ( ! forced ) {
				safeMemcpy ( sep , sepLen ) ; // "<br>"
			}

			forced = false;
		}

		// start right after breakpoint for next line
		p = breakPoint;
		cs = getUtf8CharSize(p);
		pstart = p + cs;
		// nuke it
		breakPoint = NULL;
		col = 0;
		continue;
	}

	// print out the last line which never hit the maxCharsPerLine barrier
	if ( lastRound && p - pstart ) {
		// print up to that
		safeMemcpy ( pstart , p - pstart );
	}
	
	if ( lastRound ) {
		return true;
	}

	// alloc that space. return false with g_errno set on error
	if ( brSizes && ! reserve ( brSizes ) ) {
		return false;
	}

	// reset ptrs
	p = s;
	pstart = s;
	col = 0;
	breakPoint = NULL;

	// now do it again but for real!
	lastRound = true;
	forced = false;

	goto redo;
}

#include "XmlDoc.h"

bool SafeBuf::safeTruncateEllipsis ( const char *src , int32_t maxLen ) {
	int32_t  srcLen = strlen(src);
	return safeTruncateEllipsis ( src , srcLen , maxLen );
}

bool SafeBuf::safeTruncateEllipsis ( const char *src , int32_t srcLen , int32_t maxLen ) {
	int32_t  printLen = srcLen;
	if ( printLen > maxLen ) printLen = maxLen;
	if ( ! safeMemcpy ( src , printLen ) )
		return false;
	if ( srcLen < maxLen ) {
		if ( ! nullTerm() ) return false;
		return true;
	}
	if ( ! safeMemcpy("...",3) )
		return false;
	if ( ! nullTerm() )
		return false;
	return true;
}

#include "sort.h"

bool SafeBuf::htmlDecode ( const char *src,
			   int32_t srcLen,
			   bool doSpecial ) {
	// in case we were in use
	purge();
	// make sure we have enough room
	if ( ! reserve ( srcLen + 1 ) ) return false;

	// . just decode buffer into our m_buf that we reserved
	// . it puts a \0 at the end and returns the LENGTH of the string
	//   it put into m_buf, not including the \0
	int32_t newLen = ::htmlDecode( m_buf, src, srcLen, false );

	// assign that length then
	m_length = newLen;

	// good to go
	return true;
}

void SafeBuf::replaceChar ( char src , char dst ) {
	char *px = m_buf;
	char *pxEnd = m_buf + m_length;
	for ( ; px < pxEnd ; px++ ) if ( *px == src ) *px = dst;
}


bool SafeBuf::base64Encode ( const char *sx , int32_t len ) {

	const unsigned char *s = (const unsigned char *)sx;

	if ( ! s ) return true;

	// assume all chars are double quotes and will have to be encoded
	int32_t need = len * 2 + 1 +3; // +3 for = padding

	if ( ! reserve ( need ) ) return false;

	// tmp vars
	char *dst  = m_buf + m_length;

	int32_t round = 0;

	// the table of 64 entities
	static char tab[] = {
		'A','B','C','D','E','F','G','H','I','J','K','L','M',
		'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
		'a','b','c','d','e','f','g','h','i','j','k','l','m',
		'n','o','p','q','r','s','t','u','v','w','x','y','z',
		'0','1','2','3','4','5','6','7','8','9','+','/'
	};

	unsigned char val;
	// scan through all 
	const unsigned char *send = s + len;
	for ( ; s < send ; ) {

		unsigned char c1 = s[0];
		unsigned char c2 = 0;
		//unsigned char c3 = 0;
		
		if ( s+1 < send ) c2 = s[1];
		else              c2 = 0;

		if ( round == 0 ) {
			val  = c1 >>2;
		}
		else if ( round == 1 ) {
			val  = (c1 & 0x03) << 4;
			val |=  c2 >> 4;
			// time for this
			s++;
		}
		else if ( round == 2 ) {
			val  = ((c1 & 0x0f) << 2);
			val |= ((c2 & 0xc0) >> 6);
			s++;
		}
		else if ( round == 3 ) {
			val  = (c1 & 0x3f);
			s++;
		}
		// add '0'
		*dst = tab[val];
		// point to next char
		dst++;
		// keep going if more left
		if ( s < send ) {
			// repeat every 4 cycles since it is aligned then
			if ( ++round == 4 ) round = 0;
			continue;
		}
		// if we are done do padding
		if ( round == 0 ) {
			*dst++ = '=';
		}
		if ( round == 1 ) {
			*dst++ = '=';
			*dst++ = '=';
		}
		if ( round == 2 ) {
			*dst++ = '=';
		}


	}

	m_length += dst - (m_buf + m_length);

	nullTerm();

	return true;
}

bool SafeBuf::base64Encode( const char *s ) {
	return base64Encode(s,strlen(s)); 
}

bool SafeBuf::base64Decode ( const char *src , int32_t srcLen ) {

	// make the map
	static unsigned char s_bmap[256];
	static bool s_init = false;
	if ( ! s_init ) {
		s_init = true;
		memset ( s_bmap , 0 , 256 );
		unsigned char val = 0;
		for ( unsigned char c = 'A' ; c <= 'Z'; c++ ) 
			s_bmap[c] = val++;
		for ( unsigned char c = 'a' ; c <= 'z'; c++ ) 
			s_bmap[c] = val++;
		for ( unsigned char c = '0' ; c <= '9'; c++ ) 
			s_bmap[c] = val++;
		if ( val != 62 ) gbshutdownLogicError();
		s_bmap[(unsigned char)'+'] = 62;
		s_bmap[(unsigned char)'/'] = 63;
	}

	// reserve twice as much space i guess
	if ( ! reserve ( srcLen * 2 + 1 ) ) return false;
		
	// leave room for \0
	char *dst = getBuf();
	char *dstEnd = getBufEnd(); // dst + dstSize - 5;
	nullTerm();
	unsigned char *p    = (unsigned char *)src;
	unsigned char val;
	for ( ; ; ) {
		if ( *p ) {val = s_bmap[*p]; p++; } else val = 0;
		// copy 6 bits
		*dst <<= 6;
		*dst |= val;
		if ( *p ) {val = s_bmap[*p]; p++; } else val = 0;
		// copy 2 bits
		*dst <<= 2;
		*dst |= (val>>4);
		dst++;
		// copy 4 bits
		*dst = val & 0xf;
		if ( *p ) {val = s_bmap[*p]; p++; } else val = 0;
		// copy 4 bits
		*dst <<= 4;
		*dst |= (val>>2);
		dst++;
		// copy 2 bits
		*dst = (val&0x3);
		if ( *p ) {val = s_bmap[*p]; p++; } else val = 0;
		// copy 6 bits
		*dst <<= 6;
		*dst |= val;
		dst++;
		// sanity
		if ( dst >= dstEnd ) {
			log("safebuf: bas64decode breach");
			//g_process.shutdownAbort(true);
			*dst = '\0';
			return false;
		}
		if ( ! *p ) break;
	}
	// update
	m_length = dst - m_buf;
	// null term just in case
	//dst[1] = '\0';
	nullTerm();
	return true;
}


// "ts" is a delta-t in seconds
bool SafeBuf::printTimeAgo ( int32_t ago , int32_t now , bool shorthand ) {
	// Jul 23, 1971
	if ( ! reserve2x(200) ) return false;
	// for printing
	int32_t secs = 1000;
	int32_t mins = 1000;
	int32_t hrs  = 1000;
	int32_t days = 0;
	if ( ago > 0 ) {
		secs = (int32_t)((ago)/1);
		mins = (int32_t)((ago)/60);
		hrs  = (int32_t)((ago)/3600);
		days = (int32_t)((ago)/(3600*24));
		if ( mins < 0 ) mins = 0;
		if ( hrs  < 0 ) hrs  = 0;
		if ( days < 0 ) days = 0;
	}
	bool printed = false;
	// print the time ago
	if ( shorthand ) {
		if ( mins==0 ) safePrintf("%" PRId32" secs ago",secs);
		else if ( mins ==1)safePrintf("%" PRId32" min ago",mins);
		else if (mins<60)safePrintf ( "%" PRId32" mins ago",mins);
		else if ( hrs == 1 )safePrintf ( "%" PRId32" hr ago",hrs);
		else if ( hrs < 24 )safePrintf ( "%" PRId32" hrs ago",hrs);
		else if ( days == 1 )safePrintf ( "%" PRId32" day ago",days);
		else if (days< 7 )safePrintf ( "%" PRId32" days ago",days);
		printed = true;
	}
	else {
		if ( mins==0 ) safePrintf("%" PRId32" seconds ago",secs);
		else if ( mins ==1)safePrintf("%" PRId32" minute ago",mins);
		else if (mins<60)safePrintf ( "%" PRId32" minutes ago",mins);
		else if ( hrs == 1 )safePrintf ( "%" PRId32" hour ago",hrs);
		else if ( hrs < 24 )safePrintf ( "%" PRId32" hours ago",hrs);
		else if ( days == 1 )safePrintf ( "%" PRId32" day ago",days);
		else if (days< 7 )safePrintf ( "%" PRId32" days ago",days);
		printed = true;
	}
	// do not show if more than 1 wk old! we want to seem as
	// fresh as possible

	if ( ! printed && ago > 0 ) { // && si->m_isMasterAdmin ) {
		time_t ts = now - ago;
		struct tm tm_buf;
		struct tm *timeStruct = localtime_r(&ts,&tm_buf);
		char tmp[100];
		strftime(tmp,100,"%b %d %Y",timeStruct);
		safeStrcpy(tmp);
	}
	return true;
}

bool SafeBuf::hasDigits() {
	if ( m_length <= 0 ) return false;
	for ( int32_t i = 0 ; i < m_length ; i++ )
		if ( is_digit(m_buf[i]) ) return true;
	return false;
}


int32_t SafeBuf::indexOf(char c) {
	char* p = m_buf;
	char* pend = m_buf + m_length;
	while (p < pend && *p != c) p++;
	if (p == pend) return -1;
	return p - m_buf;
}
