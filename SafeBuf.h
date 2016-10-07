#ifndef GB_SAFEBUF_H
#define GB_SAFEBUF_H

#include "gb-include.h"

/**
 * Safe Char Buffer, or mutable Strings.
 * (for java programmers, very similar to the StringBuffer class, with all the speed that c++ allows).
 * Most of strings in Gigablast are handled by those.
 */

#include "iana_charset.h"
#include "rdbid_t.h"
#include "Sanity.h"


class Words;

class SafeBuf {
public:
	//*TRUCTORS
	SafeBuf();
	SafeBuf(int32_t initSize, const char *label);

	void constructor();

	//be careful with passing in a stackBuf! it could go out
	//of scope independently of the safebuf.
	SafeBuf(char* stackBuf, int32_t cap, const char* label = NULL);
	SafeBuf(char *heapBuf, int32_t bufMax, int32_t bytesInUse, bool ownData);
	~SafeBuf();

	void setLabel ( const char *label );
	
	// CAUTION: BE CAREFUL WHEN USING THE FOLLOWING TWO FUNCTIONS!!
	// setBuf() allows you reset the contents of the SafeBuf to either
	// a stack buffer or a dynamic buffer. Only pass in true for
	// ownData if this is not a stack buffer and you are sure you
	// want SafeBuf to free the data for you. Keep in mind, all
	// previous content in SafeBuf will be cleared when you pass it
	// a new buffer.
	bool setBuf(char *newBuf, 
		    int32_t bufMax, 
		    int32_t bytesInUse, 
		    bool ownData,
		    int16_t encoding = csUTF8 );

	// set buffer from another safebuf, stealing it
	bool stealBuf ( SafeBuf *sb );

	//ACCESSORS
	char       *getBuf()       { return m_buf + m_length; }
	const char *getBuf() const { return m_buf + m_length; }
	char       *getBufPtr()       { return m_buf + m_length; }
	const char *getBufPtr() const { return m_buf + m_length; }
	char       *getBufStart()       { return m_buf; }
	const char *getBufStart() const { return m_buf; }
	char       *getBufEnd()       { return m_buf + m_capacity; }
	const char *getBufEnd() const { return m_buf + m_capacity; }
	int32_t getCapacity() const { return m_capacity; }
	int32_t getAvail() const { return m_capacity - m_length; }
	int32_t length() const { return m_length; }
	int32_t getLength() const { return m_length; }
	int32_t getBufUsed() const { return m_length; }
	void print() { 
	  if ( write(1,m_buf,m_length) != m_length) { gbshutdownAbort(true); } }

	// . returns bytes written to file, 0 is acceptable if m_length == 0
	// . returns -1 on error and sets g_errno
	int32_t saveToFile ( const char *dir, const char *filename );
	int32_t dumpToFile(const char *filename);
	int32_t save ( const char *dir, const char *fname) {return saveToFile(dir,fname); }
	int32_t save ( const char *fullFilename ) ;
	// saves to tmp file and if that succeeds then renames to orig filename
	int32_t safeSave (char *filename );

	int32_t  fillFromFile(const char *filename);
	int32_t  fillFromFile(const char *dir, const char *filename, const char *label=NULL);

	int32_t  load(const char *dir, const char *fname, const char *label = NULL) { 
		return fillFromFile(dir,fname,label);
	}

	int32_t  load(const char *fname) {
		return fillFromFile(fname);
	}

	bool safeTruncateEllipsis ( const char *src , int32_t maxLen );
	bool safeTruncateEllipsis ( const char *src , int32_t srcLen, int32_t maxLen );

	bool safeDecodeJSONToUtf8 ( const char *json, int32_t jsonLen);

	bool set ( const char *str ) {
		purge();
		if ( ! str ) return true;
		// puts a \0 at the end, but does not include it in m_length:
		return safeStrcpy ( str );
	}

	void removeLastChar ( char lastChar ) {
		if ( m_length <= 0 ) return;
		if ( m_buf[m_length-1] != lastChar ) return;
		m_length--;
		m_buf[m_length] = '\0';
	}

	//MUTATORS
	bool  safePrintf(const char *formatString, ...)
		__attribute__ ((format(printf, 2, 3)));

	bool  safeMemcpy(const void *s, int32_t len){return safeMemcpy((const char*)s,len);}
	bool  safeMemcpy(const char *s, int32_t len);
	bool  safeMemcpy_nospaces(const char *s, int32_t len);
	bool  safeMemcpy(const SafeBuf *c) { return safeMemcpy(c->m_buf,c->m_length); }
	bool  safeStrcpy ( const char *s ) ;
	//bool  safeStrcpyPrettyJSON ( char *decodedJson ) ;
	bool  safeUtf8ToJSON ( const char *utf8 ) ;
	bool jsonEncode ( const char *utf8 ) { return safeUtf8ToJSON(utf8); }
	bool jsonEncode ( char *utf8 , int32_t utf8Len );

	bool  base64Encode ( const char *s , int32_t len );
	bool  base64Decode ( const char *src , int32_t srcLen ) ;

	bool base64Encode( const char *s ) ;

	//bool  pushLong ( int32_t val ) { return safeMemcpy((char *)&val,4); }
	bool  cat(const SafeBuf& c);

	void  reset() { m_length = 0; }
	void  purge(); // Clear all data and free all allocated memory

	// . if clearIt is true we init the new buffer space to zeroes
	// . used by Collectiondb.cpp
	bool  reserve(int32_t i, const char *label=NULL , bool clearIt = false );
	bool  reserve2x(int32_t i, const char *label = NULL );

	void  incrementLength(int32_t i) { 
		m_length += i; 
		// watch out for negative i's
		if ( m_length < 0 ) m_length = 0; 
	}
	void  setLength(int32_t i) { m_length = i; }
	char *getNextLine ( char *p ) ;
	int32_t  catFile(const char *filename) ;

	void  detachBuf();
	bool  insert ( const char *s , int32_t insertPos ) ;
	bool  insert2 ( const char *s , int32_t slen, int32_t insertPos ) ;
	bool  replace ( const char *src, const char *dst ) ; // must be same lengths!
	bool removeChunk1 ( char *p , int32_t len ) ;
	bool removeChunk2 ( int32_t pos , int32_t len ) ;
	bool  safeReplace(const char *s, int32_t len, int32_t pos, int32_t replaceLen);
	bool  safeReplace2 ( const char *s, int32_t slen,
			     const char *t, int32_t tlen,
			     int32_t startOff = 0 );
	void replaceChar ( char src , char dst );

	void zeroOut() { memset ( m_buf , 0 , m_capacity ); }

	// insert <br>'s to make 's' no more than 'cols' chars per line
	bool brify2 ( const char *s, int32_t cols, const char *sep = "<br>" ,
		      bool isHtml = true ) ;

	bool brify( const char *s, int32_t slen, int32_t cols, const char *sep = "<br>",
				bool isHtml = true );

	bool hasDigits() const;

	// . like "1 minute ago" "5 hours ago" "3 days ago" etc.
	// . "ts" is the delta-t in seconds
	bool printTimeAgo (int32_t ts , int32_t now , bool shorthand = false ) ;

	// . a function for adding Tags to buffer, like from Tagdb.cpp
	// . if safebuf is a buffer of Tags from Tagdb.cpp
	class Tag *addTag2( const char *mysite,const  char *tagname, int32_t now, const char *user, int32_t ip, int32_t val,
						rdbid_t rdbId);

	class Tag *addTag3( const char *mysite, const char *tagname, int32_t now, const char *user, int32_t ip, const char *data,
						rdbid_t rdbId);

	class Tag *addTag( const char *mysite, const char *tagname, int32_t now, const char *user, int32_t ip,const  char *data,
					   int32_t dsize, rdbid_t rdbId, bool pushRdbId);

	bool addTag ( class Tag *tag );

	bool utf8Encode2( char *s, int32_t len, bool htmlEncode = false);

	bool utf32Encode(UChar32* codePoints, int32_t cpLen);

	bool htmlEncode( const char *s, int32_t len, bool encodePoundSign, int32_t truncateLen = -1 );

	bool  htmlEncode(const char *s) ;

	// html-encode any of the last "len" bytes that need it
	bool htmlEncode(int32_t len);

	bool htmlDecode (const char *s,
			 int32_t len,
			 bool doSpecial = false);

	bool  dequote ( const char *t , int32_t tlen );

	// . append a \0 but do not inc m_length
	// . for null terminating strings
	bool nullTerm ( ) {
		if(m_length >= m_capacity && !reserve(m_capacity + 1) )
			return false;
		m_buf[m_length] = '\0';
		return true;
	}

	int32_t indexOf(char c) const;

	bool  pushChar (char i) {
		if(m_length >= m_capacity) 
			if(!reserve(2*m_capacity + 1))
				return false;
		m_buf[m_length++] = i;
		// let's do this because we kinda expect it when making strings
		// and i've been burned by not having this before.
		// no, cause if we reserve just the right length, we end up
		// doing a realloc!! sux...
		//m_buf[m_length] = '\0';
		return true;
	}


	bool  pushLong (int32_t i);
	bool  pushLongLong (int64_t i);
	bool  pushFloat (float i);
	int32_t  popLong();
	float popFloat();

	int32_t  pad(const char ch, const int32_t len);

	//OPERATORS
	//copy numbers into the buffer, *in binary*
	//useful for making lists.
	bool  operator += (uint64_t i);
	bool  operator += (int64_t i);
	bool  operator += (char i);

	bool  operator += (uint32_t i);
	bool  operator += (uint16_t i);
	bool  operator += (uint8_t  i);

	bool  operator += (int32_t  i) { return *this += (uint32_t)i; }
	bool  operator += (int16_t  i) { return *this += (uint16_t)i; }
	bool  operator += (int8_t   i) { return *this += (uint8_t)i;  }

	const char& operator[](int32_t i) const;
	
public:
	int32_t  m_capacity;
	int32_t  m_length;
protected:
	char *m_buf;
public:
	const char *m_label;
	bool  m_usingStack;
	int16_t m_encoding; // output charset

	// . a special flag used by PageParser.cpp
	// . if this is true it PageParser shows the page in its html form,
	//   otherwise, if false, it converts the "<" to &lt; etc. so we see the html
	//   source view.
	// . only Words.cpp looks at this flag
	char  m_renderHtml;
};

#define STRMACRO(s) #s
#define TOKENPASTE(x, y) x ## y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)

#define StackBuf(name) char TOKENPASTE2(tmpsafebuf, __LINE__)[1024];	\
	SafeBuf name(TOKENPASTE2(tmpsafebuf, __LINE__), 1024, STRMACRO(TOKENPASTE2(__FILE__, __LINE__)))


#endif // GB_SAFEBUF_H
