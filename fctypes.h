// Matt Wells, copyright Jun 2001

#ifndef GB_FCTYPES_H
#define GB_FCTYPES_H

#include <sys/time.h>  // gettimeofday()
#include <math.h>      // floor()
#include <float.h>	// FLT_EPSILON, DBL_EPSILON
#include "Unicode.h"
#include "types.h"
#include "Sanity.h"

bool verifyUtf8 ( const char *txt ) ;
bool verifyUtf8 ( const char *txt , int32_t tlen ) ;


class SafeBuf;

// just like sprintf(s,"%"UINT64"",n), but we insert commas
int32_t ulltoa ( char *s , uint64_t n ) ;

// . convert < to &lt; and > to &gt and & to &amp;
// . store "t" into "s"
// . returns bytes stored into "s"
// . NULL terminates "s"
int32_t saftenTags ( char *dst , int32_t dstlen , const char *src , int32_t srclen ) ;

// . basically just converts "'s to &#34;'s
// . store "src" into "dest" and return bytes stored
// . does not do bounds checking in "dest"
// . used to encode things as form input variables, like query in HttpPage0.cpp
int32_t dequote       ( char *dest , char *dend , const char *src , int32_t srcLen ) ;

// . entity-ize a string so it's safe for html output
// . converts "'s to &#34;'s, &'s to &amps; <'s the &lt; and >'s to &gt;
// . store "src" into "dest" and return bytes stored
// . does not do bounds checking on "dest"
// . encode t into s
char *htmlEncode( char *dst, char *dstend, const char *src, const char *srcend );

// . like above but src is NULL terminated
// . returns length of string stored into "dest"
// . decode html entities like &amp; and &gt;
int32_t htmlDecode( char *dst, const char *src, int32_t srcLen, bool doSpecial );

// . convert " to %22 , & to %26, is that it?
// . urlEncode() stores the encoded, NULL-terminated URL in "dest"
// . requestPath leaves \0 and ? characters intact, for encoding requests
int32_t urlEncode     ( char *dest , int32_t destLen , const char *src , int32_t srcLen ,
		     bool  requestPath = false ) ;
// decode a url -- decode ALL %XX's
int32_t urlDecode ( char *dest , const char *src , int32_t tlen ) ;
int32_t urlDecodeNoZeroes ( char *dest , const char *src , int32_t tlen ) ;

bool is_urlchar(char s);

// convert hex ascii string into binary
void hexToBin ( const char *src , int32_t srcLen , char *dst );
// convert binary number of size srcLen bytes into hex string in "dst"
void binToHex ( const unsigned char *src , int32_t srcLen , char *dst );

// the _a suffix denotes an ascii string
bool has_alpha_utf8(char *s, char *send ) ;

int32_t to_lower_utf8        (char *dst , const char *src ) ;
int32_t to_lower_utf8        (char *dst , char *dstEnd, const char *src ) ;
int32_t to_lower_utf8        (char *dst , char *dstEnd, const char *src, const char *srcEnd) ;

// . get the # of words in this string
int32_t      getNumWords ( char *s , int32_t len, int32_t titleVersion ) ;
int32_t      atol2       ( const char *s, int32_t len ) ;
int64_t atoll1      ( const char *s ) ;
int64_t atoll2      ( const char *s, int32_t len ) ;
double    atof2       ( const char *s, int32_t len ) ;

char *strncasestr( char *haystack, int32_t haylen, const char *needle);
static inline const char *strncasestr( const char *haystack, int32_t haylen, const char *needle) {
	return strncasestr(const_cast<char *>(haystack), haylen, needle);
}
char *strncasestr ( char *haystack , const char *needle , int32_t haystackSize ) ;
static inline const char *strncasestr( const char *haystack, const char *needle, int32_t haystackSize ) {
	return strncasestr(const_cast<char*>(haystack),needle,haystackSize);
}
char *strncasestr( char *haystack , const char *needle, int32_t haystackSize, int32_t needleSize ) ;
static inline char *strncasestr( const char *haystack, const char *needle, int32_t haystackSize, int32_t needleSize ) {
	return strncasestr(const_cast<char*>(haystack),needle,haystackSize,needleSize);
}

static inline bool endsWith(char *haystack, int haystackLen, const char *needle, int needleLen) {
	return haystackLen >= needleLen && !strncmp(haystack + haystackLen - needleLen, needle, needleLen);
}

// https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/
static inline bool almostEqualFloat(float A, float B, float maxRelDiff = FLT_EPSILON) {
    // Calculate the difference.
    float diff = fabs(A - B);
    A = fabs(A);
    B = fabs(B);
    // Find the largest
    float largest = (B > A) ? B : A;
 
    if (diff <= largest * maxRelDiff)
        return true;
    return false;
}

static inline bool almostEqualDouble(double A, double B, double maxRelDiff = DBL_EPSILON) {
    // Calculate the difference.
    double diff = fabs(A - B);
    A = fabs(A);
    B = fabs(B);
    // Find the largest
    double largest = (B > A) ? B : A;
 
    if (diff <= largest * maxRelDiff)
        return true;
    return false;
}

// independent of case
char *gb_strcasestr ( char *haystack , const char *needle );
static inline const char *gb_strcasestr(const char *haystack, const char *needle) {
	return gb_strcasestr(const_cast<char*>(haystack),needle);
}

char *gb_strncasestr ( char *haystack , int32_t haystackSize , const char *needle ) ;
static inline const char *gb_strncasestr(const char *haystack, int32_t haystackSize, const char *needle) {
	return gb_strncasestr(const_cast<char*>(haystack),haystackSize,needle);
}

char *strnstr( const char *haystack, const char *needle, int32_t haystackLen);

// convert global to local time in milliseconds
int64_t globalToLocalTimeMilliseconds ( int64_t global ) ;
int64_t localToGlobalTimeMilliseconds ( int64_t local  ) ;

// we now default this to local time to avoid jumpiness associated with
// having to sync with host #0. most routines calling this usually are just
// taking deltas. 
int64_t gettimeofdayInMillisecondsGlobal() ; // synced with host #0
int64_t gettimeofdayInMillisecondsGlobalNoCore() ; // synced with host #0
int64_t gettimeofdayInMillisecondsSynced() ; // synced with host #0
int64_t gettimeofdayInMillisecondsLocal () ;// this is local now
int64_t gettimeofdayInMilliseconds() ;// this is local now
uint64_t gettimeofdayInMicroseconds(void) ;

// . get time in seconds since epoch
// . use this instead of call to time(NULL) cuz it uses adjustment
time_t getTime       ();  // this is local now
time_t getTimeLocal  (); 
time_t getTimeGlobal (); // synced with host #0's system clock
time_t getTimeGlobalNoCore (); // synced with host #0's system clock
time_t getTimeSynced (); // synced with host #0's system clock

int32_t stripHtml( char *content, int32_t contentLen, int32_t version, int32_t strip );

extern const unsigned char g_map_to_lower[256];
extern const unsigned char g_map_to_upper[256];
extern const char g_map_is_upper[256];
extern const char g_map_is_binary[256];
extern const char g_map_is_lower[256];
extern const char g_map_is_ascii[256];
extern const char g_map_is_punct[256];
extern const char g_map_is_alnum[256];
extern const char g_map_is_alpha[256];
extern const char g_map_is_digit[256];
extern const char g_map_is_hex[256];
extern const char g_map_is_tagname_char[256];

bool isClockInSync();

// . convert "c" to lower case
#define is_lower_a(c)          g_map_is_lower[(unsigned char)c]
#define to_lower_a(c)          g_map_to_lower[(unsigned char)c]
#define is_upper_a(c)          g_map_is_upper[(unsigned char)c]
#define to_upper_a(c)          g_map_to_upper[(unsigned char)c]
// c is latin1 in this case:
#define is_binary_a(c)         g_map_is_binary[(unsigned char)c]
#define is_wspace_a(c)         (((c)==32) || ((c)==9) || ((c)==10) || ((c)==13))
#define is_ascii(c)            (((c)>=32) && ((c)<=126))
#define is_ascii3(c)           ((unsigned char)(c)<128)
#define is_punct_a(c)          g_map_is_punct[(unsigned char)c]
#define is_alnum_a(c)          g_map_is_alnum[(unsigned char)c]
#define is_alpha_a(c)          g_map_is_alpha[(unsigned char)c]
#define is_digit(c)            g_map_is_digit[(unsigned char)c]
#define is_hex(c)              g_map_is_hex[(unsigned char)c]
#define is_tagname_char(c)     g_map_is_tagname_char[(unsigned char)c]

inline bool is_upper_utf8 ( const char *s );

// convert hex digit to value
inline int32_t htob ( char s ) {
	if ( is_digit(s) ) return s - '0';
	if ( s >= 'a'  && s <= 'f' ) return (s - 'a') + 10;
	if ( s >= 'A'  && s <= 'F' ) return (s - 'A') + 10;
	return 0;
}

inline char btoh ( char s ) {
	if ( s >= 16 ) { gbshutdownAbort(true); }
	if ( s < 10 ) return s + '0';
	return (s - 10) + 'a';
}

inline bool is_ascii2_a(const char *s, int32_t len) {
	for (int32_t i=0;i<len;i++)
		if (!is_ascii(s[i]))
			return false;
	return true;
}

inline void to_lower3_a(const char *s, int32_t len, char *buf) {
	for (int32_t i=0;i<len ;i++)
		buf[i]=to_lower_a((unsigned char)s[i]);
}

inline bool is_binary_utf8 ( const char *p ) {
	if ( getUtf8CharSize((uint8_t *)p) != 1 ) return false;
	// it is ascii, use that table now
	return is_binary_a ( *p );
}

inline bool is_lower_utf8 ( const char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) return is_lower_a ( *src );
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// is this codepoint lower?
	return ucIsLower ( x );
}

inline bool is_upper_utf8 ( const char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) return is_upper_a ( *src );
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// is this codepoint upper?
	return ucIsUpper ( x );
}

inline bool is_alnum_utf8 ( const char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) return is_alnum_a ( *src );
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// is this codepoint lower?
	return ucIsAlnum ( x );
}

inline bool is_alnum_utf8 ( const unsigned char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) return is_alnum_a ( *src );
	// convert to a code point
	UChar32 x = utf8Decode((char *)src);
	// is this codepoint lower?
	return ucIsAlnum ( x );
}

inline bool is_alpha_utf8 ( const char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) return is_alpha_a ( *src );
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// is this codepoint lower?
	return ucIsAlpha ( x );
}

inline bool is_punct_utf8 ( const char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) return is_punct_a ( *src );
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// is this codepoint lower?
	if ( ucIsAlnum ( x ) ) return false;
	else                   return true;
}

inline bool is_wspace_utf8 ( const uint8_t *src ) {
	// if in ascii do it quickly
	if ( is_ascii3(*src) ) return is_wspace_a ( *src );
	// convert to a code point
	UChar32 x = utf8Decode((char *)src);
	// is this codepoint a whitespace?
	return ucIsWhiteSpace( x );
}

inline bool is_wspace_utf8 ( const char *src ) {
	// if in ascii do it quickly
	if ( is_ascii3((uint8_t)*src) ) return is_wspace_a ( (uint8_t)*src );
	// convert to a code point
	UChar32 x = utf8Decode((char *)src);
	// is this codepoint a whitespace?
	return ucIsWhiteSpace( x );
}

uint32_t calculateChecksum(char *buf, int32_t bufLen);

// use ucIsAlnum instead...
static inline bool ucIsWordChar(UChar32 c) {
	if (!(c & 0xffffff80)) return is_alnum_a(c);
	//if (c < 256) return is_alnum(c);
	const void *p = g_ucProps.getValue(c);
	if (!p) return false;
	return *(UCProps*)p & UC_WORDCHAR;
}

// don't allow "> in our input boxes
int32_t cleanInput(char *outbuf, int32_t outbufSize, const char *inbuf, int32_t inbufLen);

// 
// these three functions replace the Msg.cpp/.h class
//
// actually "lastParm" point to the thing right after the lastParm
int32_t getMsgStoredSize(int32_t baseSize,
			 const int32_t *firstSizeParm,
			 const int32_t *lastSizeParm);
// . return ptr to the buffer we serialize into
// . return NULL and set g_errno on error
char *serializeMsg(int32_t             baseSize,
		   const int32_t      *firstSizeParm,
		   const int32_t      *lastSizeParm,
		   const char * const *firstStrPtr,
		   const void         *thisPtr,
		   int32_t            *retSize,
		   char               *userBuf,
		   int32_t             userBufSize);
char *serializeMsg ( int32_t  baseSize,
		     int32_t *firstSizeParm,
		     int32_t *lastSizeParm,
		     char **firstStrPtr,
		     void *thisPtr,
		     int32_t *retSize,
		     char *userBuf,
		     int32_t  userBufSize,
		     bool  makePtrsRefNewBuf);

char *serializeMsg2 ( void *thisPtr ,
		      int32_t objSize ,
		      char **firstStrPtr ,
		      int32_t *firstSizeParm ,
		      int32_t *retSize );

// convert offsets back into ptrs
// returns -1 on error
int32_t deserializeMsg ( int32_t  baseSize ,
		      int32_t *firstSizeParm ,
		      int32_t *lastSizeParm ,
		      char **firstStrPtr ,
		      char *stringBuf ) ;

bool deserializeMsg2 ( char **firstStrPtr , int32_t  *firstSizeParm );

#endif // GB_FCTYPES_H
