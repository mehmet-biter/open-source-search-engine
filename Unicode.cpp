#include "gb-include.h"

#include "Mem.h"
#include "HashTable.h"
#include "iana_charset.h"
#include "Titledb.h"

static HashTableX s_convTable;
// JAB: warning abatement
//static bool verifyIconvFiles();
static bool openIconvDescriptors() ;
// alias iconv_open and close to keep count of usage
// and prevent leaks..
// now just cache all iconvs in a hash table
// static iconv_t gbiconv_open(const char *tocode, const char *fromcode) ;
// static int gbiconv_close(iconv_t cd) ;


iconv_t gbiconv_open( const char *tocode, const char *fromcode) {
	// get hash for to/from
	uint32_t hash1 = hash32Lower_a(tocode, gbstrlen(tocode), 0);
	uint32_t hash2 = hash32Lower_a(fromcode, gbstrlen(fromcode),0);
	uint32_t hash = hash32h(hash1, hash2);

	g_errno = 0;
	iconv_t *convp = (iconv_t *)s_convTable.getValue(&hash);
	iconv_t conv = NULL;
	if ( convp ) conv = *convp;
	//log(LOG_DEBUG, "uni: convertor %s -> %s from hash 0x%"XINT32": 0x%"XINT32"",
	//    fromcode, tocode,
	//    hash, conv);
	if (!conv){
		//log(LOG_DEBUG, "uni: Allocating new convertor for "
		//    "%s to %s (hash: 0x%"XINT32")",
		//    fromcode, tocode,hash);
		conv = iconv_open(tocode, fromcode);
		if (conv == (iconv_t) -1) {
			log(LOG_WARN, "uni: failed to open converter for "
			    "%s to %s: %s (%d)", fromcode, tocode, 
			    strerror(errno), errno);
			// need to stop if necessary converters don't open
			//char *xx=NULL; *xx = 0;
			g_errno = errno;
			if (errno == EINVAL)
				g_errno = EBADCHARSET;
			
			return conv;
		}
		// add mem to table to keep track
		g_mem.addMem((void*)conv, 52, "iconv", 1);
		// cache convertor
		s_convTable.addKey(&hash, &conv);
		//log(LOG_DEBUG, "uni: Saved convertor 0x%"INT32" under hash 0x%"XINT32"",
		//    conv, hash);
	}
	else{
		// reset convertor
		char *dummy = NULL;
		size_t dummy2 = 0;
		// JAB: warning abatement
		//size_t res = iconv(conv,NULL,NULL,&dummy,&dummy2);
		iconv(conv,NULL,NULL,&dummy,&dummy2);
	}

	return conv;
}

int gbiconv_close(iconv_t cd) {
	/// @todo ALC gbiconv_close currently does nothing
	//int val = iconv_close(cd);
	//if (val  == 0) g_mem.rmMem((void*)cd, 1, "iconv", 1);
	//return val;	
	return 0;
}

void gbiconv_reset(){
	for (int32_t i=0;i<s_convTable.getNumSlots();i++){
		//int32_t key = *(int32_t *)s_convTable.getKey(i);
		//if (!key) continue;
		if ( ! s_convTable.m_flags[i] ) continue;
		iconv_t *pconv = (iconv_t *)s_convTable.getValueFromSlot(i);
		if (! pconv) continue;
		iconv_t iconv = *pconv;
		//logf(LOG_DEBUG, "iconv: freeing iconv: 0x%x", (int)iconv);
		g_mem.rmMem((void*)iconv, 52, "iconv");
		libiconv_close(iconv);
	}
	s_convTable.reset();
}

#undef iconv_open
#define iconv_open(to, from) ((iconv_t)coreme(0))
#undef iconv_close
#define iconv_close(cd) ((int)coreme(0))




#define MAX_BAD_CHARS 500

#define VERIFY_UNICODE_CHECKSUMS 1

#define CHKSUM_UPPERMAP          1241336150
#define CHKSUM_LOWERMAP          1023166806
#define CHKSUM_PROPERTIES        33375957
#define CHKSUM_COMBININGCLASS    526097805
#define CHKSUM_SCRIPTS           1826246000
#define CHKSUM_KDMAP             1920116453

bool ucInit(char *path, bool verifyFiles){

	char file[384];
	if (path == NULL) path = "./";

	// Might want to move this out of ucInit someday
	// but right now it's the only thing that uses .so files (?)
	char gbLibDir[512];
	snprintf(gbLibDir, 512, "%s/lib",path);
	// i don't think this is used any more because we don't have it!
	//log(LOG_INIT, "ucinit: Setting LD_RUN_PATH to \"%s\"",gbLibDir);
	if (setenv("LD_RUN_PATH", gbLibDir, 1)){
		log(LOG_INIT, "Failed to set LD_RUN_PATH");
	}
	//char *ldpath = getenv("LD_RUN_PATH");
	// i don't think this is used any more because we don't have it!
	//log(LOG_DEBUG, "ucinit: LD_RUN_PATH: %s\n", ldpath);


	strcpy(file, path);
	strcat(file, "/ucdata/uppermap.dat");
	if (!loadUnicodeTable(&g_ucUpperMap,file, 
			      VERIFY_UNICODE_CHECKSUMS, 
			      CHKSUM_UPPERMAP))
		goto failed;
	strcpy(file, path);
	strcat(file, "/ucdata/lowermap.dat");
	if (!loadUnicodeTable(&g_ucLowerMap,file, 
			      VERIFY_UNICODE_CHECKSUMS, 
			      CHKSUM_LOWERMAP))
		goto failed;
	strcpy(file, path);
	strcat(file, "/ucdata/properties.dat");
	if (!loadUnicodeTable(&g_ucProps, file, 
			      VERIFY_UNICODE_CHECKSUMS, 
			      CHKSUM_PROPERTIES))
		goto failed;

	strcpy(file, path);
	strcat(file, "/ucdata/scripts.dat");
	if (!loadUnicodeTable(&g_ucScripts, file, 
			      VERIFY_UNICODE_CHECKSUMS, 
			      CHKSUM_SCRIPTS))
		goto failed;

	// MDW: do we need this for converting from X to utf8? or for
	// the is_alnum(), etc. functions?
	if (!loadDecompTables(path)) {
		goto failed;
	}

	//s_convTable.set(1024);
	if ( ! s_convTable.set(4,sizeof(iconv_t),1024,NULL,0,false,0,"cnvtbl"))
		goto failed;
	
	// dont use these files anymore
	if (verifyFiles){
		if (!openIconvDescriptors())
			return log(LOG_WARN,
				   "uni: unable to open all iconv descriptors");
	}		

	return true;
	
failed:
	return log(LOG_WARN, 
		   "uni: unable to load all property tables");
}

char *ucDetectBOM(char *buf, int32_t bufsize){
	if (bufsize < 4) return NULL;
	// copied from ICU
	if(buf[0] == '\xFE' && buf[1] == '\xFF') {
		return  "UTF-16BE";
	} else if(buf[0] == '\xFF' && buf[1] == '\xFE') {
		if(buf[2] == '\x00' && buf[3] =='\x00') {
			return "UTF-32LE";
		} else {
			return  "UTF-16LE";
		}
	} else if(buf[0] == '\xEF' && buf[1] == '\xBB' && buf[2] == '\xBF') {
		return  "UTF-8";
	} else if(buf[0] == '\x00' && buf[1] == '\x00' && 
		  buf[2] == '\xFE' && buf[3]=='\xFF') {
		return  "UTF-32BE";
	}

	return NULL;
}

int32_t ucToAny(char *outbuf, int32_t outbufsize, char *charset_out,
		 char *inbuf, int32_t inbuflen, char *charset_in,
		 int32_t ignoreBadChars , int32_t niceness ){
	if (inbuflen == 0) return 0;
	// alias for iconv
	char *csAlias = charset_in;
	if (!strncmp(charset_in, "x-windows-949", 13))
		csAlias = "CP949";

	// Treat all latin1 as windows-1252 extended charset
	if (!strncmp(charset_in, "ISO-8859-1", 10) )
		csAlias = "WINDOWS-1252";
	
	iconv_t cd = gbiconv_open(charset_out, csAlias);
	int32_t numBadChars = 0;
	if (cd == (iconv_t)-1) {	
		log("uni: Error opening input conversion"
		    " descriptor for %s: %s (%d)\n", 
		    charset_in,
		    strerror(errno),errno);
		return 0;		
	}

	//if (normalized) *normalized = false;
	char *pin = (char*)inbuf;
	size_t inRemaining = inbuflen;
	char *pout = (char*)outbuf;
	size_t outRemaining = outbufsize;
	int res = 0;
	if (outbuf == NULL || outbufsize == 0) {
		// just find the size needed for conversion
#define TMP_SIZE 32
		char buf[TMP_SIZE];
		int32_t len = 0;
		while (inRemaining) {
			QUICKPOLL(niceness);
			pout = buf;
			outRemaining = TMP_SIZE;
			res = iconv(cd, &pin, &inRemaining, 
				    &pout, &outRemaining);
			if (res < 0 && errno){
				// convert the next TMP_SIZE block
				if (errno == E2BIG) { 
					len += TMP_SIZE; 
					continue;
				}
				gbiconv_close(cd);
				return 0; // other error
			}
			len += TMP_SIZE-outRemaining;
			//len >>= 1; // sizeof UChar
			len += 1; // NULL terminated
			gbiconv_close(cd);
			return len;			
		}
	}

	while (inRemaining && outRemaining) {
		QUICKPOLL(niceness);
		//printf("Before - in: %d, out: %d\n", 
		//inRemaining, outRemaining);
		res = iconv(cd,&pin, &inRemaining,
				&pout, &outRemaining);

		if (res < 0 && errno){
			//printf("errno: %s (%d)\n", strerror(errno), errno);
			g_errno = errno;
			switch(errno) {
			case EILSEQ:
				numBadChars++;

 				if (ignoreBadChars >= 0 &&
				    numBadChars > ignoreBadChars) goto done;
				utf8Encode('?', pout);
				pout++;outRemaining --;
 				pin++; inRemaining--;
				g_errno = 0;
 				continue;
			case EINVAL:
				numBadChars++;

				utf8Encode('?', pout); 
				pout++;outRemaining --;
				pin++; inRemaining--;
				g_errno=0;
				continue;
				// go ahead and flag an error now
				// if there is a bad character, we've 
				// probably misguessed the charset

			case E2BIG:
				//log("uni: error converting to UTF-8: %s",
				//    strerror(errno));
				goto done;
			default:
				log("uni: unknown error occurred "
				    "converting to UTF-8: %s (%d)",
				    strerror(errno), errno);
				goto done;
			}
		}
	}
done:
	gbiconv_close(cd);
	int32_t len =  (outbufsize - outRemaining) ;
	len = len>=outbufsize-1?outbufsize-2:len;
	//len >>= 1;
	//len = outbuf[len]=='\0'?len-1:len;
	outbuf[len] = '\0';
	static char eflag = 1;
	if (numBadChars) {
		if ( eflag )
			log(LOG_DEBUG, "uni: ucToAny: got %"INT32" bad chars "
			    "in conversion 2. Only reported once.",
			    numBadChars);
		// this flag makes it so no bad characters are reported
		// in subsequent conversions
		//eflag = 0;
	}
	if (res < 0 && g_errno) return 0; 
	return len ;
}

int32_t stripAccentMarks (char *outbuf, int32_t outbufsize,
		       unsigned char *p, int32_t inbuflen) {
	char *s = (char *)p;
	char *send = (char *)p + inbuflen;
	int32_t cs;
	char *dst = outbuf;
	for ( ; s < send ; s += cs ) {
		// how big is this character?
		cs = getUtf8CharSize(s);
		// convert the utf8 character to UChar32
		UChar32 uc = utf8Decode ( s );
		// break "uc" into decomposition of UChar32s
		UChar32 ttt[32];
		int32_t klen = recursiveKDExpand(uc,ttt,32);
		if(klen>32){char *xx=NULL;*xx=0;}
		// sanity
		if ( dst + 5 > outbuf+outbufsize ) return -1;
		// if the same, leave it! it had no accent marks or other
		// modifiers...
		if ( klen <= 1 ) {
			gbmemcpy ( dst , s , cs );
			dst += cs;
			continue;
		}
		// take the first one as the stripped
		// convert back to utf8
		int32_t stored = utf8Encode ( ttt[0] , dst );
		// skip over the stored utf8 char
		dst += stored;
	}
	// sanity. breach check
	if ( dst > outbuf+outbufsize ) { char *xx=NULL;*xx=0; }
	// return # of bytes stored into outbuf
	return dst - outbuf;
}

static iconv_t cd_latin1_u8 = (iconv_t)-1;
int32_t latin1ToUtf8(char *outbuf, int32_t outbufsize, 
		  char *inbuf, int32_t inbuflen){
	if ( cd_latin1_u8 < 0) {
		cd_latin1_u8 = gbiconv_open("UTF-8", "WINDOWS-1252");
		if ( cd_latin1_u8 < 0) {	
			log("uni: Error opening output conversion"
			    " descriptor for utf-8: %s (%d)\n", 
			    strerror(g_errno),g_errno);
			return 0;		
		}
	}

	char *pin = (char*)inbuf;
	size_t inRemaining = inbuflen;
	char *pout = outbuf;
	size_t outRemaining = outbufsize;
	while (inRemaining && outRemaining) {
		int res = iconv(cd_latin1_u8,&pin, &inRemaining,
				&pout, &outRemaining);
		if (res < 0 && errno){
			switch(errno) {
			case EILSEQ:
			case EINVAL:
				log(LOG_DEBUG, 
				    "uni: Bad character in utf-8 conversion");
				*pout++ = '?';outRemaining--;
				pin++; inRemaining--;
				continue;
			case E2BIG:
				// this happens a bunch when we are guessing
				// the charset i think, so don't spam the
				// log with warning, keep it a LOG_INFO
				// I'm making this a log debug --zak
				log(LOG_DEBUG,
				    "uni: error converting to utf-8: %s",
				    strerror(errno));
				goto done;
			default:
				log("uni: unknown error occurred "
				    "converting to utf-8: %s (%d)",
				    strerror(errno), errno);
				goto done;
			}
		}
	}
done:
	int32_t len =  outbufsize - outRemaining;
	len = len>=outbufsize?outbufsize-1:len;
	//len = outbuf[len]=='\0'?len-1:len;
	outbuf[len] = '\0';
	return len;
	
}

void resetUnicode ( ) {
	//s_convTable.reset();
	gbiconv_reset();
}

bool openIconvDescriptors() {

	// why do this when we call gbiconv_open() directly from ucToAny()
	// and other functions?
	return true;

	for (int i=2; i <= 2258 ; i++ ){
		if (!supportedCharset(i)) continue;

		char *charset = get_charset_str(i);
		if (!charset) return false;
		
		char *csAlias = charset;
		if (!strncmp(charset, "x-windows-949", 13))
			csAlias = "CP949";
		
		// Treat all latin1 as windows-1252 extended charset
		if (!strncmp(charset, "ISO-8859-1", 10) )
			csAlias = "WINDOWS-1252";
		if (!strncmp(charset, "Windows-31J", 13)){
			csAlias = "CP932";
		}

		iconv_t cd1 = gbiconv_open("UTF-8", csAlias);
		if (cd1 == (iconv_t)-1) {	
		 	//return false;
		}

		iconv_t cd2 = gbiconv_open(csAlias, "UTF-8");
		if (cd2 == (iconv_t)-1) {	
			//return false;
		}
	}
	// ...and the ones that don't involve utf16
	if (gbiconv_open("UTF-8", "WINDOWS-1252") < 0) return false;
	if (gbiconv_open("WINDOWS-1252", "UTF-8") < 0) return false;
	
	//log(LOG_INIT, "uni: Successfully loaded all iconv descriptors");
	return true;
}
