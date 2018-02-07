#include "utf8_convert.h"
#include "HashTableX.h"


static HashTableX s_convTable;


static iconv_t gbiconv_open( const char *tocode, const char *fromcode) {
	// get hash for to/from
	uint32_t hash1 = hash32Lower_a(tocode, strlen(tocode), 0);
	uint32_t hash2 = hash32Lower_a(fromcode, strlen(fromcode),0);
	uint32_t hash = hash32h(hash1, hash2);

	g_errno = 0;
	iconv_t *convp = (iconv_t *)s_convTable.getValue(&hash);
	iconv_t conv = NULL;
	if ( convp ) conv = *convp;
	//log(LOG_DEBUG, "uni: convertor %s -> %s from hash 0x%" PRIx32": 0x%" PRIx32,
	//    fromcode, tocode,
	//    hash, conv);
	if (!conv){
		//log(LOG_DEBUG, "uni: Allocating new convertor for "
		//    "%s to %s (hash: 0x%" PRIx32")",
		//    fromcode, tocode,hash);
		conv = iconv_open(tocode, fromcode);
		if (conv == (iconv_t) -1) {
			log(LOG_WARN, "uni: failed to open converter for "
			    "%s to %s: %s (%d)", fromcode, tocode, 
			    strerror(errno), errno);
			g_errno = errno;
			if (errno == EINVAL)
				g_errno = EBADCHARSET;
			
			return conv;
		}
		// cache convertor
		s_convTable.addKey(&hash, &conv);
		//log(LOG_DEBUG, "uni: Saved convertor 0x%" PRId32" under hash 0x%" PRIx32,
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

static int gbiconv_close(iconv_t cd) {
	/// @todo ALC gbiconv_close currently does nothing
	//int val = iconv_close(cd);
	//return val;	
	return 0;
}

static void gbiconv_reset() {
	for (int32_t i=0;i<s_convTable.getNumSlots();i++){
		//int32_t key = *(int32_t *)s_convTable.getKey(i);
		//if (!key) continue;
		if ( ! s_convTable.m_flags[i] ) continue;
		iconv_t *pconv = (iconv_t *)s_convTable.getValueFromSlot(i);
		if (! pconv) continue;
		iconv_t iconv = *pconv;
		//logf(LOG_DEBUG, "iconv: freeing iconv: 0x%x", (int)iconv);
		iconv_close(iconv);
	}
	s_convTable.reset();
}


static int32_t ucToAny(char *outbuf, int32_t outbufsize, const char *charset_out,
		const char *inbuf, int32_t inbuflen, const char *charset_in,
		 int32_t ignoreBadChars ){
	if (inbuflen == 0) return 0;
	// alias for iconv
	const char *csAlias = charset_in;
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
	char *pin = const_cast<char*>(inbuf); //const cast due to iconv() speciality
	size_t inRemaining = inbuflen;
	char *pout = outbuf;
	size_t outRemaining = outbufsize;
	int res = 0;
	if (outbuf == NULL || outbufsize == 0) {
		// just find the size needed for conversion
#define TMP_SIZE 32
		char buf[TMP_SIZE];
		int32_t len = 0;
		while (inRemaining) {
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
			len += 1; // NULL terminated
			gbiconv_close(cd);
			return len;			
		}
	}

	while (inRemaining && outRemaining) {
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
	if (numBadChars) {
		log(LOG_DEBUG, "uni: ucToAny: got %" PRId32" bad chars in conversion 2.",
		    numBadChars);
	}
	if (res < 0 && g_errno) return 0; 
	return len ;
}


int32_t ucToUtf8(char *outbuf, int32_t outbuflen,
		const char *inbuf, int32_t inbuflen,
		const char *charset, int32_t ignoreBadChars) {
  return ucToAny(outbuf, outbuflen, "UTF-8", inbuf, inbuflen, charset, ignoreBadChars);
}


bool utf8_convert_initialize() {
	if(! s_convTable.set(4,sizeof(iconv_t),1024,NULL,0,false,"cnvtbl"))
		return false;
	return true;
}


void utf8_convert_finalize() {
	gbiconv_reset();
}
