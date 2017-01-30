#include "gb-include.h"

#include "Loop.h"
#include "Entities.h"
#include "SafeBuf.h"
#include "Xml.h"
#include "XmlNode.h"
#include "Conf.h"
#include "Process.h"
#include "Hostdb.h"
#include "Mem.h"
#include <fcntl.h>

static bool g_clockInSync = false;

bool isClockInSync() { 
	if ( g_hostdb.m_initialized && g_hostdb.m_hostId == 0 ) return true;
	return g_clockInSync; 
}


// . put all the maps here now
// . convert "c" to lower case
const unsigned char g_map_to_lower[256] = {
	  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
	 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
	 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
	 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
	'@','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',
	'p','q','r','s','t','u','v','w','x','y','z', 91, 92, 93, 94, 95,
	 96,'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',
	'p','q','r','s','t','u','v','w','x','y','z',123,124,125,126,127,
	128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,
	144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
	160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,
	176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
	224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
	240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,223,
	224,225,226,227,228,229,230,231,232,233,234,235,236,237,238,239,
	240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
};

// converts ascii chars and IS_O chars to their lower case versions
const unsigned char g_map_to_upper[256] = {
	  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15,
	 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
	 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
	 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63,
	 64,'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O',
	'P','Q','R','S','T','U','V','W','X','Y','Z', 91, 92, 93, 94, 95,
	96, 'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O',
	'P','Q','R','S','T','U','V','W','X','Y','Z',123,124,125,126,127,
	128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,
	144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,
	160,161,162,163,164,165,166,167,168,169,170,171,172,173,174,175,
	176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,
	192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
	208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,
	192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
	208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,255
};

const char g_map_is_upper[256] = {
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

// people mix windows 1252 into latin-1 so we have to be less restrictive here...
const char g_map_is_binary[256] = {
	1,1,1,1,1,1,1,1,1,0,0,1,1,0,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
	0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,0,0,0,0,0,0,1,0,0,1,1,0,0,1,1,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};


// converts ascii chars and IS_O chars to their lower case versions
const char g_map_is_lower[256] = { // 97-122 and 224-255 (excluding 247)
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1
};

const char g_map_is_ascii[256] = { // 32 to 126
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

const char g_map_is_punct[256] = { // 33-47, 58-64, 91-96, 123-126, 161-191, 215,247
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,
	1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,
	1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0
};

const char g_map_is_alnum[256] = { // 48-57, 65-90,97-122,192-255(excluding 215,247)
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
	0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
	0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1
};

const char g_map_is_alpha[256] = { // 65-90, 97-122, 192-255 (excluding 215, 247)
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
	0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1
};

const char g_map_is_digit[256] = { // 48-57
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};


const char g_map_is_hex[256] = { // 48-57
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,
	0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

// stolen from is_alnum, but turned on - and _
// 48-57, 65-90,97-122,192-255(excluding 215,247)
// we include the : for feedburner:origlink
const char g_map_is_tagname_char [256] = {
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
	1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
	0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,
	0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
	1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};


char *strncasestr( char *haystack, int32_t haylen, const char *needle){
	int32_t matchLen = 0;
	int32_t needleLen = strlen(needle);
	for (int32_t i = 0; i < haylen;i++){
		char c1 = to_lower_a(haystack[i]);
		char c2 = to_lower_a(needle[matchLen]);
		if ( c1 != c2 ){
			// no match
			matchLen = 0;
			continue;
		}
		// we matched another character
		matchLen++;
		if (matchLen < needleLen) continue;
		
		// we've matched the whole string
		return haystack + i - matchLen + 1;
	}
	return NULL;
}

char *strnstr( const char *haystack, const char *needle, int32_t haystackLen ) {
	int32_t matchLen = 0;
	int32_t needleLen = strlen( needle );
	for ( int32_t i = 0; i < haystackLen; ++i ) {
		char c1 = ( haystack[ i ] );
		char c2 = ( needle[ matchLen ] );
		if ( c1 != c2 ) {
			// no match
			if (matchLen != 0) {
				i -= matchLen;
				matchLen = 0;
			}
			continue;
		}

		// we matched another character
		matchLen++;
		if ( matchLen < needleLen ) {
			continue;
		}

		// we've matched the whole string
		return const_cast<char*>( haystack + i - matchLen + 1 );
	}

	return NULL;
}

// . get the # of words in this string
int32_t getNumWords ( char *s , int32_t len, int32_t titleVersion ) {

	int32_t wordCount = 0;
	bool inWord   = false;
	for ( int32_t i = 0 ; i < len ; i++ ) {
		if ( ! is_alnum_a ( s[i] ) && s[i]!='\'' ) {
			inWord = false;
			continue;
		}
		if ( ! inWord ) {
			inWord = true;
			wordCount++;
		}
	}
	return wordCount;
}

// . this stores a "n" into "s" and returns the # of bytes written into "s"
// . it also puts commas into the number
// . it now also NULL terminates bytes written into "s"
int32_t ulltoa ( char *s , uint64_t n ) {
	// if n is zero, it's easy
	if ( n == 0LL ) { *s++='0'; *s='\0'; return 1; }
	// a hunk is a number in [0,999]
	int32_t hunks[10]; 
	int32_t lastHunk = -1;
	// . get the hunks
	// . the first hunk we get is called the "lowest hunk"
	// . "lastHunk" is called the "highest hunk"
	for ( int32_t i = 0 ; i < 10 ; i++ ) {
		hunks[i] = n % 1000;
		n /= 1000;
		if ( hunks[i] != 0 ) lastHunk = i;
	}
	// remember start of buf for calculating # bytes written
	char *start = s;
	// print the hunks separated by comma
	for ( int32_t i = lastHunk ; i >= 0 ; i-- ) {
		// pad all hunks except highest hunk with zeroes
		if ( i != lastHunk ) sprintf ( s , "%03" PRId32 , hunks[i] );
		else                 sprintf ( s , "%" PRId32 , hunks[i] );
		s += strlen(s);
		// comma after all hunks but lowest hunk
		if ( i != 0 ) *s++ = ',';
	}
	// null terminate it
	*s = '\0';
	// return # of bytes stored into "s"
	return s - start;
}

int32_t atol2 ( const char *s, int32_t len ) {
	// skip over spaces
	const char *end = s + len;
	while ( s < end && is_wspace_a ( *s ) ) s++;
	// return 0 if all spaces
	if ( s == end ) return 0;
	int32_t i   = 0;
	int32_t val = 0;
	bool negative = false;
	if ( s[0] == '-' ) { negative = true; i++; }
	while ( i < len && is_digit(s[i]) ) val = val * 10 + ( s[i++] - '0' );
	if ( negative ) return -val;
	return val;
}

int64_t atoll1 ( const char *s ) {
	return atoll ( s );
}

int64_t atoll2 ( const char *s, int32_t len ) {
	// skip over spaces
	const char *end = s + len;
	while ( s < end && is_wspace_a ( *s ) ) s++;
	// return 0 if all spaces
	if ( s == end ) return 0;
	int32_t i   = 0;
	int64_t val = 0LL;
	bool negative = false;
	if ( s[0] == '-' ) { negative = true; i++; }
	while ( i < len && is_digit(s[i]) ) val = val * 10LL + ( s[i++] - '0');
	if ( negative ) return -val;
	return val;
}

double atof2 ( const char *s, int32_t len ) {
	// skip over spaces
	const char *end = s + len;
	while ( s < end && is_wspace_a ( *s ) ) { s++; len--; }
	// return 0 if all spaces
	if ( s == end ) return 0;
	char tmpBuf[128];
	if ( len >= 128 ) len = 127;
	//strncpy ( dst , s , len );

	const char *p = s;
	const char *srcEnd = s + len;
	char *dst = tmpBuf;
	// remove commas
	for ( ; p < srcEnd ; p++ ) {
		// skip commas
		if ( *p == ',' ) continue;
		// otherwise store it
		*dst++ = *p;
	}
	// null term
	*dst = '\0';
	//buf[len] = '\0';
	return atof ( tmpBuf );
}

// convert hex ascii string into binary at "dst"
void hexToBin ( const char *src , int32_t srcLen , char *dst ) {
	const char *srcEnd = src + srcLen;
	for ( ; src && src < srcEnd ; ) {
		*dst  = htob(*src++);
		*dst <<= 4;
		*dst |= htob(*src++);
		dst++;
	}
	// sanity check
	if ( src != srcEnd ) { g_process.shutdownAbort(true); }
}

void binToHex ( const unsigned char *src , int32_t srcLen , char *dst ) {
	const unsigned char *srcEnd = src + srcLen;
	for ( ; src && src < srcEnd ; ) {
		*dst++ = btoh(*src>>4);
		*dst++ = btoh(*src&15);
		src++;
	}
	// always null term!
	*dst = '\0';
	// sanity check
	if ( src != srcEnd ) { g_process.shutdownAbort(true); }
}


// . like strstr but haystack may not be NULL terminated
// . needle, however, IS null terminated
char *strncasestr ( char *haystack , const char *needle , int32_t haystackSize ) {
	int32_t needleSize = strlen(needle);
	int32_t n = haystackSize - needleSize ;
	for ( int32_t i = 0 ; i <= n ; i++ ) {
		// keep looping if first chars do not match
		if ( to_lower_a(haystack[i]) != to_lower_a(needle[0]) ) {
			continue;
		}

		// if needle was only 1 char it's a match
		if ( ! needle[1] ) {
			return &haystack[i];
		}

		// compare the whole strings now
		if ( strncasecmp ( &haystack[i] , needle , needleSize ) == 0 ) {
			return &haystack[i];
		}
	}
	return NULL;
}

// . like strstr but haystack may not be NULL terminated
// . needle, however, IS null terminated
char *strncasestr ( char *haystack , const char *needle , 
		    int32_t haystackSize, int32_t needleSize ) {
	int32_t n = haystackSize - needleSize ;
	for ( int32_t i = 0 ; i <= n ; i++ ) {
		// keep looping if first chars do not match
		if ( to_lower_a(haystack[i]) != to_lower_a(needle[0]) ) 
			continue;
		// if needle was only 1 char it's a match
		if ( ! needle[1] ) return &haystack[i];
		// compare the whole strings now
		if ( strncasecmp ( &haystack[i] , needle , needleSize ) == 0 ) 
			return &haystack[i];			
	}
	return NULL;
}

// independent of case
char *gb_strcasestr ( char *haystack , const char *needle ) {
	int32_t needleSize   = strlen(needle);
	int32_t haystackSize = strlen(haystack);
	int32_t n = haystackSize - needleSize ;
	for ( int32_t i = 0 ; i <= n ; i++ ) {
		// keep looping if first chars do not match
		if ( to_lower_a(haystack[i]) != to_lower_a(needle[0]) ) 
			continue;
		// if needle was only 1 char it's a match
		if ( ! needle[1] ) return &haystack[i];
		// compare the whole strings now
		if ( strncasecmp ( &haystack[i] , needle , needleSize ) == 0 ) 
			return &haystack[i];			
	}
	return NULL;
}


char *gb_strncasestr ( char *haystack , int32_t haystackSize , const char *needle ) {
	// temp term
	char c = haystack[haystackSize];
	haystack[haystackSize] = '\0';
	char *res = gb_strcasestr ( haystack , needle );
	haystack[haystackSize] = c;
	return res;
}

// . convert < to &lt; and > to &gt
// . store "t" into "s"
// . returns bytes stored into "s"
// . NULL terminates "s" if slen > 0
int32_t saftenTags ( char *dst , int32_t dstlen , const char *src , int32_t srclen ) {
	char *start = dst ;
	// leave a char for the \0
	char *dstend  = dst + dstlen - 1;
	const char *srcend  = src + srclen;
	for ( ; src < srcend && dst + 4 < dstend ; src++ ) {
		if ( *src == '<' ) {
			*dst++ = '&';
			*dst++ = 'l';
			*dst++ = 't';
			*dst++ = ';';
			continue;
		}			
		if ( *src == '>' ) {
			*dst++ = '&';
			*dst++ = 'g';
			*dst++ = 't';
			*dst++ = ';';
			continue;
		}			
		*dst++ = *src;
	}
	// NULL terminate "dst"
	*dst = '\0';
	// return # of bytes, excluding \0, stored into s
	return dst - start;
}

// . if "doSpecial" is true, then we don't touch &lt;, &gt; and &amp;
int32_t htmlDecode( char *dst, const char *src, int32_t srcLen, bool doSpecial ) {
	//special-case optimization
	if ( srcLen == 0 ) {
		return 0;
	}

	char * const start  = dst;
	const char * const srcEnd = src + srcLen;
	for ( ; src < srcEnd ; ) {
		
		if ( *src != '&' ) {
			*dst++ = *src++;
		} else {
			// Ok, we have an ampersand. So decode it into unicode/utf8, do a few special
			// checks, and in general store the resulting string in dst[]
		
			// store decoded entity char into dst[j]
			uint32_t codepoint[2];
			int32_t codepointCount;
			int32_t utf8Len=0;

			// "skip" is how many bytes the entites was in "src"
			int32_t skip = getEntity_a( src, srcEnd - src, codepoint, &codepointCount, &utf8Len );

			// If the entity is invalid/unknown then store it as text

			//@todo BR: Temporary fix for named html entities where the utf8 length is 
			// longer than the html entity name. This causes problems for XmlDoc that
			// calls this function with the same buffer as input and output
			if ( skip == 0 || utf8Len > skip) {
				//todo: if doSpecial then make it an &amp;
				// but the decoding is done in-place (bad idea) so we cannot expand the output
				*dst++ = *src++;
				continue;
			}

			// . special mapping
			// . make &lt; and &gt; special so Xml::set() still works
			// . and make &amp; special so we do not screw up summaries
			if ( doSpecial ) {
				if ( codepoint[0] == '<' || codepoint[0] == '>' || codepoint[0] == '&' ) {
					int32_t entityLen = 4;
					const char* entityStr = "";
	
					if (codepoint[0] == '<') {
						entityStr = "&lt;";
					} else if (codepoint[0] == '>') {
						entityStr = "&gt;";
					} else {
						entityStr = "&amp;";
						entityLen = 5;
					}
	
					memcpy(dst, entityStr, entityLen);
					src += skip;
					dst += entityLen;
					continue;
				}
	
				/// @todo verify if we need to replace " with '
	
				// some tags have &quot; in their value strings
				// so we have to preserve that!
				// use curling quote:
				//http://www.dwheeler.com/essays/quotes-test-utf-8.html
				// curling double and single quotes resp:
				// &ldquo; &rdquo; &lsquo; &rdquo;
				if ( codepoint[0] == '\"' ) {
					*dst = '\'';
					dst++;
					src += skip;
					continue;
				}
			}

			int32_t totalUtf8Bytes = 0;
			for ( int i=0; i<codepointCount; i++) {
				// . store it into "dst" in utf8 format
				int32_t numBytes = utf8Encode ( codepoint[i], dst );
				totalUtf8Bytes += numBytes;

				// sanity check. do not eat our tail if dst == src
				if ( totalUtf8Bytes > skip ) {
					g_process.shutdownAbort(true);
				}

				// advance dst ptr
				dst += numBytes;
			}

			// skip over the encoded entity in the source string
			src += skip;
		}
	}

	// NUL term
	*dst = '\0';

	return dst - start;
}

// . make something safe as an form input value by translating the quotes
// . store "t" into "s" and return bytes stored
// . does not do bounds checking
int32_t dequote ( char *s , char *send , const char *t , int32_t tlen ) {
	char *start = s;
	const char *tend = t + tlen;
	for ( ; t < tend && s < send ; t++ ) {
		if ( *t == '"' ) {
			if ( s + 5 >= send ) return 0;
			*s++ = '&';
			*s++ = '#';
			*s++ = '3';
			*s++ = '4';
			*s++ = ';';
			continue;
		}
		*s++ = *t;		
	}
	// all or nothing
	if ( s + 1 >= send ) return 0;
	*s = '\0';
	return s - start;
}


// . entity-ize a string so it's safe for html output
// . store "t" into "s" and return bytes stored
// . does bounds checking
char *htmlEncode ( char *dst, char *dstend, const char *src, const char *srcend ) {
	for ( ; src < srcend ; src++ ) {
		if ( dst + 7 >= dstend ) {
			*dst = '\0';
			return dst;
		}

		if ( *src == '"' ) {
			*dst++ = '&';
			*dst++ = '#';
			*dst++ = '3';
			*dst++ = '4';
			*dst++ = ';';
			continue;
		}
		if ( *src == '<' ) {
			*dst++ = '&';
			*dst++ = 'l';
			*dst++ = 't';
			*dst++ = ';';
			continue;
		}
		if ( *src == '>' ) {
			*dst++ = '&';
			*dst++ = 'g';
			*dst++ = 't';
			*dst++ = ';';
			continue;
		}
		if ( *src == '&' ) {
			*dst++ = '&';
			*dst++ = 'a';
			*dst++ = 'm';
			*dst++ = 'p';
			*dst++ = ';';
			continue;
		}
		if ( *src == '#' ) {
			*dst++ = '&';
			*dst++ = '#';
			*dst++ = '0';
			*dst++ = '3';
			*dst++ = '5';
			*dst++ = ';';
			continue;
		}
		*dst++ = *src;		
	}
	*dst = '\0';
	return dst;
}



//Note: there is a safer version in GbUtil.* that writes to a SafeBuf.
// . convert "-->%22 , &-->%26, +-->%2b, space-->+, ?-->%3f is that it?
// . convert so we can display as a cgi PARAMETER within a url
// . used by HttPage2 (cached web page) to encode the query into a url
// . used by PageRoot to do likewise
// . returns bytes written into "d" not including terminating \0
int32_t urlEncode ( char *d , int32_t dlen , const char *s , int32_t slen, bool requestPath ) {
	char *dstart = d;
	// subtract 1 to make room for a terminating \0
	char *dend = d + dlen - 1;
	const char *send = s + slen;
	for ( ; s < send && d < dend ; s++ ) {
		if ( *s == '\0' && requestPath ) {
			*d++ = *s;
			continue;
		}
		// encode if not fit for display
		if ( ! is_ascii ( *s ) ) goto encode;
		switch ( *s ) {
		case ' ': goto encode;
		case '&': goto encode;
		case '"': goto encode;
		case '+': goto encode;
		case '%': goto encode;
		case '#': goto encode;
		// encoding < and > are more for displaying on an
		// html page than sending to an http server
		case '>': goto encode;
		case '<': goto encode;
		case '?': if ( requestPath ) break;
			  goto encode;
		}
		// otherwise, no need to encode
		*d++ = *s;
		continue;
	encode:
		// space to +
		if ( *s == ' ' && d + 1 < dend ) { *d++ = '+'; continue; }
		// break out if no room to encode
		if ( d + 2 >= dend ) break;
		*d++ = '%';
		// store first hex digit
		unsigned char v = ((unsigned char)*s)/16 ;
		if ( v < 10 ) v += '0';
		else          v += 'A' - 10;
		*d++ = v;
		// store second hex digit
		v = ((unsigned char)*s) & 0x0f ;
		if ( v < 10 ) v += '0';
		else          v += 'A' - 10;
		*d++ = v;
	}
	// NULL terminate it
	*d = '\0';
	// and return the length
	return d - dstart;
}

// . decodes "s/slen" and stores into "dest"
// . returns the number of bytes stored into "dest"
int32_t urlDecode ( char *dest , const char *s , int32_t slen ) {
	int32_t j = 0;
	for ( int32_t i = 0 ; i < slen ; i++ ) {
		if ( s[i] == '+' ) { dest[j++]=' '; continue; }
		dest[j++] = s[i];
		if ( s[i]  != '%'  ) continue;
		if ( i + 2 >= slen ) continue;
		// if two chars after are not hex chars, it's not an encoding
		if ( ! is_hex ( s[i+1] ) ) continue;
		if ( ! is_hex ( s[i+2] ) ) continue;
		// convert hex chars to values
		unsigned char a = htob ( s[i+1] ) * 16; 
		unsigned char b = htob ( s[i+2] )     ;
		dest[j-1] = (char) (a + b);
		i += 2;
	}
	return j;
}


int32_t urlDecodeNoZeroes ( char *dest , const char *s , int32_t slen ) {
	int32_t j = 0;
	for ( int32_t i = 0 ; i < slen ; i++ ) {
		if ( s[i] == '+' ) { dest[j++]=' '; continue; }
		dest[j++] = s[i];
		if ( s[i]  != '%'  ) continue;
		if ( i + 2 >= slen ) continue;
		// if two chars after are not hex chars, it's not an encoding
		if ( ! is_hex ( s[i+1] ) ) continue;
		if ( ! is_hex ( s[i+2] ) ) continue;
		// convert hex chars to values
		unsigned char a = htob ( s[i+1] ) * 16; 
		unsigned char b = htob ( s[i+2] )     ;
		// NO ZEROES! fixes &content= having decoded \0's in it
		// and setting our parms
		if ( a + b == 0 ) {
			log("fctypes: urlDecodeNoZeros encountered url "
			    "encoded zero. truncating http request.");
			return j; 
		}
		dest[j-1] = (char) (a + b);
		i += 2;
	}
	return j;
}

static int64_t s_adjustment = 0;

int64_t globalToLocalTimeMilliseconds ( int64_t global ) {
	return global - s_adjustment;
}

int64_t localToGlobalTimeMilliseconds ( int64_t local ) {
	return local + s_adjustment;
}

static char s_tafile[1024];
static bool s_hasFileName = false;

// returns false and sets g_errno on error
bool setTimeAdjustmentFilename ( const char *dir, const char *filename ) {
	s_hasFileName = true;
	int32_t len1 = strlen(dir);
	int32_t len2 = strlen(filename);
	if ( len1 + len2 > 1000 ) { g_process.shutdownAbort(true); }
	sprintf(s_tafile,"%s/%s",dir,filename);
	return true;
}

// returns false and sets g_errno on error
bool loadTimeAdjustment ( ) {
	// bail if no filename to read
	if ( ! s_hasFileName ) return true;
	// read it in
	// one line in text
	int fd = open ( s_tafile , O_RDONLY );
	if ( fd < 0 ) {
		log("util: could not open %s for reading",s_tafile);
		g_errno = errno;
		return false;
	}
	char rbuf[1024+1];
	// read in max bytes
	ssize_t bytes_read = read ( fd , rbuf , sizeof(rbuf)-1 );
	if ( bytes_read < 0 ) {
		log(LOG_WARN, "util: reading %s had error: %s",s_tafile,
		    mstrerror(errno));
		close(fd);
		g_errno = errno;
		return false;
	}
	close(fd);
	rbuf[(size_t)bytes_read] = '\0';
	
	// parse the text line
	int64_t stampTime = 0LL;
	int64_t clockAdj  = 0LL;
	if(sscanf ( rbuf , "%" PRIu64" %" PRId64, &stampTime, &clockAdj ) != 2) {
		log("util: Could not parse content of %s", s_tafile);
		g_errno = errno;
		return false;
	}
	// get stamp age
	int64_t local = gettimeofdayInMillisecondsLocal();
	int64_t stampAge = local - stampTime;
	// if too old forget about it
	if ( stampAge > 2*86400 ) return true;
	// update adjustment
	s_adjustment = clockAdj;
	// if stamp in file is within 2 days old, assume its still good
	// this will prevent having to rebuild a sortbydatetable
	// and really slow down loadups
	g_clockInSync = true;
	// note it
	log(LOG_DEBUG, "util: loaded %s and put clock in sync. age=%" PRIu64" adj=%" PRId64,
	    s_tafile,stampAge,clockAdj);
	return true;
}

// . returns false and sets g_errno on error
// . saved by Process::saveBlockingFiles1()
bool saveTimeAdjustment ( ) {
	// fortget it if setTimeAdjustmentFilename never called
	if ( ! s_hasFileName ) return true;
	// must be in sync!
	if ( ! g_clockInSync ) return true;
	// store it
	uint64_t local = gettimeofdayInMillisecondsLocal();
	char wbuf[1024];
	sprintf (wbuf,"%" PRIu64" %" PRId64"\n",local,s_adjustment);
	// write it out
	int fd = open ( s_tafile , O_CREAT|O_WRONLY|O_TRUNC , 0666 );
	if ( fd < 0 ) {
		log("util: could not open %s for writing",s_tafile);
		g_errno = errno;
		return false;
	}
	// how many bytes to write?
	int32_t len = strlen(wbuf);
	// read in max bytes
	int nw = write ( fd , wbuf , len );
	if ( nw != len ) {
		log(LOG_WARN, "util: writing %s had error: %s",s_tafile,
		    mstrerror(errno));
		close(fd);
		g_errno = errno;
		return false;
	}
	close(fd);
	// note it
	log(LOG_DEBUG, "util: saved %s",s_tafile);
	// it was written ok
	return true;
}

// a "fake" settimeofdayInMilliseconds()
void settimeofdayInMillisecondsGlobal ( int64_t newTime ) {
	// this isn't async signal safe...
	struct timeval tv;
	gettimeofday ( &tv , NULL );
	int64_t now=(int64_t)(tv.tv_usec/1000)+((int64_t)tv.tv_sec)*1000;
	// bail if no change... UNLESS we need to sync clock!!
	if ( s_adjustment == newTime - now && g_clockInSync ) return;
	// log it, that way we know if there is another issue
	// with flip-flopping (before we synced with host #0 and also
	// with proxy #0)
	int64_t delta = s_adjustment - (newTime - now) ;
	if ( delta > 100 || delta < -100 )
		logf(LOG_INFO,"gb: Updating clock adjustment from "
		     "%" PRId64" ms to %" PRId64" ms", s_adjustment , newTime - now );
	// set adjustment
	s_adjustment = newTime - now;
	// return?
	if ( g_clockInSync ) return;
	// we are now in sync
	g_clockInSync = true;
	// log it
	if ( s_hasFileName )
		logf(LOG_INFO,"gb: clock is now synced with host #0. "
		     "saving to %s",s_tafile);
	else
		logf(LOG_INFO,"gb: clock is now synced with host #0.");
	// save
	saveTimeAdjustment();
	// force timedb to load now!
	//initAllSortByDateTables ( );
}

time_t getTimeGlobal() {
	return gettimeofdayInMillisecondsSynced() / 1000;
}

time_t getTimeGlobalNoCore() {
	return gettimeofdayInMillisecondsGlobalNoCore() / 1000;
}

time_t getTimeSynced() {
	return gettimeofdayInMillisecondsSynced() / 1000;
}

int64_t gettimeofdayInMillisecondsGlobal() {
	return gettimeofdayInMillisecondsSynced();
}

int64_t gettimeofdayInMillisecondsSynced() {
	// sanity check
	if ( ! isClockInSync() ) { 
		static int s_printed = 0;
		if ( (s_printed % 100) == 0 ) {
			log("xml: clock not in sync with host #0 yet!!!!!!");
		}
		s_printed++;
	}

	int64_t now;

	struct timeval tv;
	gettimeofday ( &tv , NULL );
	now = (int64_t)(tv.tv_usec/1000)+((int64_t)tv.tv_sec)*1000;

	// adjust from Msg0x11 time adjustments
	now += s_adjustment;
	return now;
}

int64_t gettimeofdayInMillisecondsGlobalNoCore() {
	// this isn't async signal safe...
	struct timeval tv;
	gettimeofday ( &tv , NULL );

	int64_t now=(int64_t)(tv.tv_usec/1000)+((int64_t)tv.tv_sec)*1000;

	// adjust from Msg0x11 time adjustments
	now += s_adjustment;
	return now;
}

int64_t gettimeofdayInMillisecondsLocal() {
	return gettimeofdayInMilliseconds();
}

uint64_t gettimeofdayInMicroseconds(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return(((uint64_t)tv.tv_sec * 1000000LL) + (uint64_t)tv.tv_usec);
}

// "local" means the time on this machine itself, NOT a timezone thing.
int64_t gettimeofdayInMilliseconds() {
	struct timeval tv;
	gettimeofday ( &tv , NULL );
	return ((int64_t)(tv.tv_usec/1000)+((int64_t)tv.tv_sec)*1000);
}

time_t getTime () {
	return getTimeLocal();
}

// . get time in seconds
time_t getTimeLocal () {
	// get time now
	uint32_t now = gettimeofdayInMilliseconds() / 1000;
	return (time_t)now;
}

void getCalendarFromMs(int64_t ms, 
		       int32_t* days, 
		       int32_t* hours, 
		       int32_t* minutes, 
		       int32_t* secs,
		       int32_t* msecs) {
	int32_t s =     1000;
	int32_t m = s * 60;
	int32_t h = m * 60;
	int32_t d = h * 24;

	*days = ms / d;
	int64_t tmp = ms % d;
	*hours = tmp / h;
	tmp = tmp % h;
	*minutes = tmp / m;
	tmp = tmp % m;
	*secs = tmp / s;
	
	*msecs = tmp % s;
}

uint32_t calculateChecksum(char *buf, int32_t bufLen){
	uint32_t sum = 0;
	for(int32_t i = 0; i < bufLen>>2;i++)
		sum += ((uint32_t*)buf)[i];
	return sum;
}

bool has_alpha_utf8 ( char *s , char *send ) {
	char cs = 0;
	for ( ; s < send ; s += cs ) {
		cs = getUtf8CharSize ( s );
		if ( cs == 1 ) {
			if (is_alpha_a(*s)) return true;
			continue;
		}
		if ( is_alpha_utf8(s) ) return true;
	}
	return false;
}


// . returns bytes stored into "dst" from "src"
// . just do one character, which may be from 1 to 4 bytes
int32_t to_lower_utf8(char *dst, const char *src) {
	// if in ascii do it quickly
	if(is_ascii3(*src)) {
		*dst = to_lower_a ( *src );
		return 1;
	}
	// convert to a code point
	UChar32 x = utf8Decode(src);
	// covert to lower
	UChar32 y = ucToLower ( x );
	// put it back to utf8. return bytes stored.
	return utf8Encode(y, dst);
}

int32_t to_lower_utf8(char *dst, char * /*dstEnd*/, const char *src, const char *srcEnd) {
	char *dstart = dst;
	for ( ; src < srcEnd ; src += getUtf8CharSize((uint8_t *)src) )
		dst += to_lower_utf8 ( dst , src );
	// return bytes written
	return dst - dstart;
}

int32_t to_lower_utf8(char *dst, char * /*dstEnd*/, const char *src ) {
	char *dstart = dst;
	for ( ; *src ; src += getUtf8CharSize((uint8_t *)src) )
		dst += to_lower_utf8 ( dst , src );
	// return bytes written
	return dst - dstart;
}

// currently unused
// int32_t to_upper_utf8(char *dst, char *src) {
// 	// if in ascii do it quickly
// 	if(is_ascii3(*src)) {
// 		*dst = to_upper_a ( *src );
// 		return 1;
// 	}
// 	// convert to a code point
// 	UChar32 x = utf8Decode(src);
// 	// covert to lower
// 	UChar32 y = ucToUpper(x);
// 	// put it back to utf8. return bytes stored.
// 	return utf8Encode(y, dst);
// }


#include "HttpMime.h" // CT_HTML

// returns length of stripped content, but will set g_errno and return -1
// on error
int32_t stripHtml( char *content, int32_t contentLen, int32_t version, int32_t strip ) {
	if ( !strip ) {
		log( LOG_WARN, "query: html stripping not required!" );
		return contentLen;
	}
	if ( ! content )
		return 0;
	if ( contentLen == 0 )
		return 0;

	// filter content if we should
	// keep this on the big stack so "content" still references something
	Xml tmpXml;
	// . get the content as xhtml (should be NULL terminated)
	// . parse as utf8 since all we are doing is messing with 
	//   the tags...content manipulation comes later
	if ( !tmpXml.set( content, contentLen, version, CT_HTML ) ) {
		return -1;
	}

	//if( strip == 4 )
	//	return tmpXml.getText( content, contentLen );

	// go tag by tag
	int32_t     n       = tmpXml.getNumNodes();
	XmlNode *nodes   = tmpXml.getNodes();
	// Xml class may have converted to utf16
	content    = tmpXml.getContent();
	contentLen = tmpXml.getContentLen();
	char    *x       = content;
	char    *xend    = content + contentLen;
	int32_t     stackid = -1;
	int32_t     stackc  =  0;
	char     skipIt  =  0;
	// . hack COL tag to NOT require a back tag
	// . do not leave it that way as it could mess up our parsing
	//g_nodes[25].m_hasBackTag = 0;
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// get id of this node
		int32_t id = nodes[i].m_nodeId;
		
		// if strip is 4, just remove the script tag
		if( strip == 4 ){
			if ( id ){
				if ( id == TAG_SCRIPT ){
					skipIt ^= 1;
					continue;
				}
			}
			else if ( skipIt ) continue;
			goto keepit;
		}
		
		// if strip is 3, ALL tags will be removed!
		if( strip == 3 ) {
			if( id ) {
				// . we dont want anything in between:
				//   - script tags (83)
				//   - style tags  (111)
				if ((id == TAG_SCRIPT) || (id == TAG_STYLE)) skipIt ^= 1;
				// save img to have alt text kept.
				if ( id == TAG_IMG  ) goto keepit;
				continue;
			}
			else {
				if( skipIt ) continue;
				goto keepit;
			}
		}
		// get it
		int32_t fk;
		if   ( strip == 1 ) fk = g_nodes[id].m_filterKeep1;
		else                fk = g_nodes[id].m_filterKeep2;
		// if tag is <link ...> only keep it if it has
		// rel="stylesheet" or rel=stylesheet
		if ( strip == 2 && id == TAG_LINK ) { // <link> tag id
			int32_t   fflen;
			char *ff = nodes[i].getFieldValue ( "rel" , &fflen );
			if ( ff && fflen == 10 &&
			     strncmp(ff,"stylesheet",10) == 0 )
				goto keepit;
		}
		// just remove just the tag if this is 2
		if ( fk == 2 ) continue;
		// keep it if not in a stack
		if ( ! stackc && fk ) goto keepit;
		// if no front/back for tag, just skip it
		if ( ! nodes[i].m_hasBackTag ) continue;
		// start stack if none
		if ( stackc == 0 ) {
			// but not if this is a back tag
			if ( nodes[i].m_node[1] == '/' ) continue;
			// now start the stack
			stackid = id;
			stackc  =  1;
			continue;
		}
		// skip if this tag does not match what is on stack
		if ( id != stackid ) continue;
		// if ANOTHER front tag, inc stack
		if ( nodes[i].m_node[1] != '/' ) stackc++;
		// otherwise, dec the stack count
		else                             stackc--;
		// . ensure not negative from excess back tags
		// . reset stackid to -1 to indicate no stack
		if ( stackc <= 0 ) { stackid= -1; stackc = 0; }
		// skip it
		continue;
	keepit:
		// replace images with their alt text
		int32_t vlen;
		char *v;
		if ( id == TAG_IMG ) {
			v = nodes[i].getFieldValue("alt", &vlen );
			// try title if no alt text
			if ( ! v )
				v = nodes[i].getFieldValue("title", &vlen );
			if ( v ) { gbmemcpy ( x, v, vlen ); x += vlen; }
			continue;
		}
		// remove background image from body,table,td tags
		if ( id == TAG_BODY || id == TAG_TABLE || id == TAG_TD ) {
			v = nodes[i].getFieldValue("background", &vlen);
			// remove background, just sabotage it
			if ( v ) v[-4] = 'x';
		}
		// store it
		gbmemcpy ( x , nodes[i].m_node , nodes[i].m_nodeLen );
		x += nodes[i].m_nodeLen;
		// sanity check
		if ( x > xend ) { g_process.shutdownAbort(true);}
	}
	contentLen = x - content;
	content [ contentLen ] = '\0';
	// unhack COL tag
	//g_nodes[25].m_hasBackTag = 1;
	return contentLen;
}


bool is_urlchar(char s) {
	// [a-z0-9/:_-.?$,~=#&%+@]
	if(isalnum(s)) return true;
	if(s == '/' ||
	   s == ':' ||
	   s == '_' ||
	   s == '-' ||
	   s == '.' ||
	   s == '?' ||
	   s == '$' ||
	   s == ',' ||
	   s == '~' ||
	   s == '=' ||
	   s == '#' ||
	   s == '&' ||
	   s == '%' ||
	   s == '+' ||
	   s == '@') return true;
	return false;
}
// don't allow "> in our input boxes
int32_t cleanInput(char *outbuf, int32_t outbufSize, const char *inbuf, int32_t inbufLen){
	char *p = outbuf;
	int32_t numQuotes=0;
	int32_t lastQuote = 0;
	for (int32_t i=0;i<inbufLen;i++){
		if (p-outbuf >= outbufSize-1) break;
			
		if (inbuf[i] == '"'){
			numQuotes++;
			lastQuote = i;
		}
		// if we have an odd number of quotes and a close angle bracket
		// it could be an xss attempt
		if (inbuf[i] == '>' && (numQuotes & 1)) {
			p = outbuf+lastQuote;
			break;
		}
		*p = inbuf[i];
		p++;
	}
	*p = '\0';
	return p-outbuf;
}


//
// get rid of the virtual Msg class because it screws up how we
// serialize/deserialize everytime we compile gb it seems
//

int32_t getMsgStoredSize(int32_t baseSize,
			 const int32_t *firstSizeParm,
			 const int32_t *lastSizeParm) {
	int32_t size = baseSize;
	// add up string buffer sizes
	const int32_t *sizePtr = firstSizeParm;
	const int32_t *sizeEnd = lastSizeParm;
	for ( ; sizePtr <= sizeEnd ; sizePtr++ )
		size += *sizePtr;
	return size;
}

// . return ptr to the buffer we serialize into
// . return NULL and set g_errno on error
char *serializeMsg(int32_t             baseSize,
		   const int32_t      *firstSizeParm,
		   const int32_t      *lastSizeParm,
		   const char * const *firstStrPtr,
		   const void         *thisPtr,
		   int32_t            *retSize,
		   char               *userBuf,
		   int32_t             userBufSize)
{
	return serializeMsg(baseSize,
	                    const_cast<int32_t*>(firstSizeParm),
			    const_cast<int32_t*>(lastSizeParm),
			    const_cast<char**>(firstStrPtr),
			    const_cast<void*>(thisPtr),
			    retSize,
			    userBuf,
			    userBufSize,
			    false);
}

char *serializeMsg ( int32_t  baseSize ,
		     int32_t *firstSizeParm ,
		     int32_t *lastSizeParm ,
		     char **firstStrPtr ,
		     void *thisPtr ,
		     int32_t *retSize     ,
		     char *userBuf     ,
		     int32_t  userBufSize ,
		     bool  makePtrsRefNewBuf ) {
	// make a buffer to serialize into
	char *buf  = NULL;
	//int32_t  need = getStoredSize();
	int32_t need = getMsgStoredSize(baseSize,firstSizeParm,lastSizeParm);
	// big enough?
	if ( need <= userBufSize ) buf = userBuf;
	// alloc if we should
	if ( ! buf ) buf = (char *)mmalloc ( need , "Ra" );
	// bail on error, g_errno should be set
	if ( ! buf ) return NULL;
	// set how many bytes we will serialize into
	*retSize = need;
	// copy the easy stuff
	char *p = buf;
	gbmemcpy ( p , (char *)thisPtr , baseSize );//getBaseSize() );
	p += baseSize; // getBaseSize();
	// then store the strings!
	int32_t  *sizePtr = firstSizeParm;//getFirstSizeParm(); // &size_qbuf;
	int32_t  *sizeEnd = lastSizeParm;//getLastSizeParm (); // &size_displayMet
	char **strPtr  = firstStrPtr;//getFirstStrPtr  (); // &ptr_qbuf;
	for ( ; sizePtr <= sizeEnd ;  ) {
		// if we are NULL, we are a "bookmark", so
		// we alloc'd space for it, but don't copy into
		// the space until after this call toe serialize()
		if ( ! *strPtr ) goto skip;
		// sanity check -- cannot copy onto ourselves
		if ( p > *strPtr && p < *strPtr + *sizePtr ) {
			g_process.shutdownAbort(true); }
		// copy the string into the buffer
		gbmemcpy ( p , *strPtr , *sizePtr );
	skip:
		// . make it point into the buffer now
		// . MDW: why? that is causing problems for the re-call in
		//   Msg3a, it calls this twice with the same "m_r"
		if ( makePtrsRefNewBuf ) *strPtr = p;
		// advance our destination ptr
		p += *sizePtr;
		// advance both ptrs to next string
		sizePtr++;
		strPtr++;
	}
	return buf;
}

char *serializeMsg2 ( void *thisPtr ,
		      int32_t objSize ,
		      char **firstStrPtr ,
		      int32_t *firstSizeParm ,
		      int32_t *retSize ) {

	// make a buffer to serialize into
	int32_t baseSize = (char *)firstStrPtr - (char *)thisPtr;
	char **endStrPtr = (char**)firstSizeParm; //last+1
	int nptrs = endStrPtr - firstStrPtr;
	int32_t need = baseSize;
	need += nptrs * sizeof(char *);
	need += nptrs * sizeof(int32_t);
	// tally up the string sizes
	int32_t  *srcSizePtr = (int32_t *)firstSizeParm;
	char **srcStrPtr  = (char **)firstStrPtr;
	int32_t totalStringSizes = 0;
	for ( int i = 0 ; i < nptrs ; i++ ) {
		if ( srcStrPtr[i] == NULL ) continue;
		totalStringSizes += srcSizePtr[i];

	}
	int32_t stringBufferOffset = need;
	need += totalStringSizes;
	// alloc serialization buffer
	char *buf = (char *)mmalloc ( need , "sm2" );
	// bail on error, g_errno should be set
	if ( ! buf ) return NULL;
	// set how many bytes we will serialize into
	*retSize = need;
	// copy everything over except strings themselves
	char *p = buf;
	gbmemcpy ( p , (char *)thisPtr , stringBufferOffset );//need );
	// point to the string buffer
	p += stringBufferOffset;
	// then store the strings!
	char **dstStrPtr = (char **)(buf + baseSize );
	int32_t *dstSizePtr = (int32_t *)(buf + baseSize+sizeof(char *)*nptrs);
	for ( int count = 0 ; count < nptrs ; count++ ) {
		// copy ptrs
		//*dstStrPtr = *srcStrPtr;
		//*dstSizePtr = *srcSizePtr;
		// if we are NULL, we are a "bookmark", so
		// we alloc'd space for it, but don't copy into
		// the space until after this call toe serialize()
		if ( ! *srcStrPtr )
			goto skip;
		// if this is valid then size can't be 0! fix upstream.
		if ( ! *srcSizePtr ) { g_process.shutdownAbort(true); }
		// if size is 0 use strlen. helps with InjectionRequest
		// where we set ptr_url or ptr_content but not size_url, etc.
		//if ( ! *srcSizePtr )
		//	*srcSizePtr = strlen(*strPtr);
		// sanity check -- cannot copy onto ourselves
		if ( p > *srcStrPtr && p < *srcStrPtr + *srcSizePtr ) {
			g_process.shutdownAbort(true); }
		// copy the string into the buffer
		gbmemcpy ( p , *srcStrPtr , *srcSizePtr );
	skip:
		// point it now into the string buffer
		*dstStrPtr = p;
		// if it is 0 length, make ptr NULL in destination
		if ( *srcSizePtr == 0 || *srcStrPtr == NULL ) {
			*dstStrPtr = NULL;
			*dstSizePtr = 0;
		}
		// advance our destination ptr
		p += *dstSizePtr;
		// advance both ptrs to next string
		srcSizePtr++;
		srcStrPtr++;
		dstSizePtr++;
		dstStrPtr++;
	}
	return buf;
}


// convert offsets back into ptrs
int32_t deserializeMsg ( int32_t  baseSize ,
		      int32_t *firstSizeParm ,
		      int32_t *lastSizeParm ,
		      char **firstStrPtr ,
		      char *stringBuf ) {
	// point to our string buffer
	char *p = stringBuf;//getStringBuf(); // m_buf;
	// then store the strings!
	int32_t  *sizePtr = firstSizeParm;//getFirstSizeParm(); // &size_qbuf;
	int32_t  *sizeEnd = lastSizeParm;//getLastSizeParm (); // &size_displayMet
	char **strPtr  = firstStrPtr;//getFirstStrPtr  (); // &ptr_qbuf;
	for ( ; sizePtr <= sizeEnd ;  ) {
		// convert the offset to a ptr
		*strPtr = p;
		// make it NULL if size is 0 though
		if ( *sizePtr == 0 ) *strPtr = NULL;
		// sanity check
		if ( *sizePtr < 0 ) { g_errno = ECORRUPTDATA; return -1;}
		// advance our destination ptr
		p += *sizePtr;
		// advance both ptrs to next string
		sizePtr++;
		strPtr++;
	}
	// return how many bytes we processed
	return baseSize + (p - stringBuf);//getStringBuf());
}

bool deserializeMsg2 ( char    **firstStrPtr , // ptr_url
		       int32_t  *firstSizeParm ) { // size_url
	int nptrs=((char *)firstSizeParm-(char *)firstStrPtr)/sizeof(char *);
	// point to our string buffer
	char *p = ((char *)firstSizeParm + sizeof(int32_t)*nptrs);
	// then store the strings!
	int32_t  *sizePtr = firstSizeParm;//getFirstSizeParm(); // &size_qbuf;
	//int32_t  *sizeEnd = lastSizeParm;//getLastSizeParm (); // &size_displ
	char **strPtr  = firstStrPtr;//getFirstStrPtr  (); // &ptr_qbuf;
	int count = 0;
	for ( ; count < nptrs ; count++ ) { // sizePtr <= sizeEnd ;  ) {
		// convert the offset to a ptr
		*strPtr = p;
		// make it NULL if size is 0 though
		if ( *sizePtr == 0 ) *strPtr = NULL;
		// sanity check
		if ( *sizePtr < 0 ) return false;//{ g_process.shutdownAbort(true); }
		// advance our destination ptr
		p += *sizePtr;
		// advance both ptrs to next string
		sizePtr++;
		strPtr++;
	}
	// return how many bytes we processed
	//return baseSize + (p - stringBuf);//getStringBuf());
	return true;
}

bool verifyUtf8 ( const char *txt , int32_t tlen ) {
	if ( ! txt  || tlen <= 0 ) return true;
	char size;
	const char *p = txt;
	const char *pend = txt + tlen;
	for ( ; p < pend ; p += size ) {
		size = getUtf8CharSize(p);
		// skip if ascii
		if ( ! (p[0] & 0x80) ) continue;
		// ok, it's a utf8 char, it must have both hi bits set
		if ( (p[0] & 0xc0) != 0xc0 ) return false;
		// if only one byte, we are done..  how can that be?
		if ( size == 1 ) return false;
		//if ( ! utf8IsSane ( p[0] ) ) return false;
		// successive utf8 chars must have & 0xc0 be equal to 0x80
		// but the first char it must equal 0xc0, both set
		if ( (p[1] & 0xc0) != 0x80 ) return false;
		if ( size == 2 ) continue;
		if ( (p[2] & 0xc0) != 0x80 ) return false;
		if ( size == 3 ) continue;
		if ( (p[3] & 0xc0) != 0x80 ) return false;
	}
	if ( p != pend ) return false;
	return true;
}

bool verifyUtf8 ( const char *txt ) {
	int32_t tlen = strlen(txt);
	return verifyUtf8(txt,tlen);
}
