#include "GbEncoding.h"
#include "HttpMime.h"
#include "iana_charset.h"
#include "ByteOrderMark.h"
#include "fctypes.h"

#include "Log.h"
#include <cstdlib>

#include "third-party/compact_enc_det/compact_enc_det/compact_enc_det.h"

static uint16_t convertEncodingCED(Encoding encoding) {
	switch (encoding) {
		case ISO_8859_1:
			return csISOLatin1;
		case ISO_8859_2:
			return csISOLatin2;
		case ISO_8859_3:
			return csISOLatin3;
		case ISO_8859_4:
			return csISOLatin4;
		case ISO_8859_5:
			return csISOLatinCyrillic;
		case ISO_8859_6:
			return csISOLatinArabic;
		case ISO_8859_7:
			return csISOLatinGreek;
		case ISO_8859_8:
			return csISOLatinHebrew;
		case ISO_8859_9:
			return csISOLatin5;
		case ISO_8859_10:
			return cslatin6;
		case JAPANESE_EUC_JP:
			return csEUCJP;
		case JAPANESE_SHIFT_JIS:
			return csxsjis;
		case JAPANESE_JIS:
			return csJISEncoding;
		case CHINESE_BIG5:
			return csBig5;
		case CHINESE_GB:
			return csISO58GB231280;
//		case CHINESE_EUC_CN:       = 15,  // Misnamed. Should be EUC_TW

		case KOREAN_EUC_KR:
			return csEUCKR;
		case UNICODE:
			return csUnicode;

//		case CHINESE_EUC_DEC:      = 18,  // Misnamed. Should be EUC_TW
//		case CHINESE_CNS:          = 19,  // Misnamed. Should be EUC_TW
//		case CHINESE_BIG5_CP950:   = 20,  // Teragram BIG5_CP950
//		case JAPANESE_CP932:       = 21,  // Teragram CP932

		case UTF8:
			return csUTF8;
		case UNKNOWN_ENCODING:
			return csUnknown;
		case ASCII_7BIT:
			return csASCII;
		case RUSSIAN_KOI8_R:
			return csKOI8R;
		case RUSSIAN_CP1251:
			return cswindows1251;
		case MSFT_CP1252:
			return cswindows1252;
		case RUSSIAN_KOI8_RU:
			return csKOI8U;
		case MSFT_CP1250:
			return cswindows1250;
		case ISO_8859_15:
			return csISO885915;
		case MSFT_CP1254:
			return cswindows1254;
		case MSFT_CP1257:
			return cswindows1257;
		case ISO_8859_11:
		case MSFT_CP874:
			return csTIS620;
		case MSFT_CP1256:
			return cswindows1256;
		case MSFT_CP1255:
			return cswindows1255;
		case ISO_8859_8_I:
			return csISO88598I;
		case HEBREW_VISUAL:
			return csISOLatinHebrew;
		case CZECH_CP852:
			return csPCp852;
		case CZECH_CSN_369103:
			return csISO139CSN369103;
		case MSFT_CP1253:
			return cswindows1253;
		case RUSSIAN_CP866:
			return csIBM866;
		case ISO_8859_13:
			return csISO885913;
		case ISO_2022_KR:
			return csISO2022KR;
		case GBK:
			return csGBK;
		case GB18030:
			return csGB18030;
		case BIG5_HKSCS:
			return csBig5HKSCS;
		case ISO_2022_CN:
			return csISO2022CN;

//		case TSCII                = 49,
//		case TAMIL_MONO           = 50,
//		case TAMIL_BI             = 51,
//		case JAGRAN               = 52,

		case MACINTOSH_ROMAN:
			return csMacintosh;
		case UTF7:
			return csUTF7;

//		case BHASKAR              = 55,  // Indic encoding - Devanagari
//		case HTCHANAKYA           = 56,  // 56 Indic encoding - Devanagari

		case UTF16BE:
			return csUTF16BE;
		case UTF16LE:
			return csUTF16LE;
		case UTF32BE:
			return csUTF32BE;
		case UTF32LE:
			return csUTF32LE;

//		case BINARYENC            = 61,

		case HZ_GB_2312:
			return csHZGB2312;

//		case UTF8UTF8             = 63,
//		case TAM_ELANGO           = 64,  // Elango - Tamil
//		case TAM_LTTMBARANI       = 65,  // Barani - Tamil
//		case TAM_SHREE            = 66,  // Shree - Tamil
//		case TAM_TBOOMIS          = 67,  // TBoomis - Tamil
//		case TAM_TMNEWS           = 68,  // TMNews - Tamil
//		case TAM_WEBTAMIL         = 69,  // Webtamil - Tamil
//		case KDDI_SHIFT_JIS       = 70,
//		case DOCOMO_SHIFT_JIS     = 71,
//		case SOFTBANK_SHIFT_JIS   = 72,
//		case KDDI_ISO_2022_JP     = 73,
//		case SOFTBANK_ISO_2022_JP = 74,
		default:
			return csUnknown;
	}
}

uint16_t GbEncoding::getCharset(HttpMime *mime, const char *url, const char *s, int32_t slen) {
	int16_t httpHeaderCharset = csUnknown;
	int16_t unicodeBOMCharset = csUnknown;
	int16_t metaCharset = csUnknown;
	bool invalidUtf8Encoding = false;

	int16_t charset = csUnknown;

	if ( slen < 0 ) slen = 0;

	const char *pstart = s;
	const char *pend   = s + slen;

	char mime_charset[32];
	const char *mime_charset_ptr = NULL;
	const char *cs    = mime->getCharset();
	size_t cslen = mime->getCharsetLen();
	if(cslen > sizeof(mime_charset)-1)
		cslen = sizeof(mime_charset)-1;
	if ( cs && cslen > 0 ) {
		charset = get_iana_charset ( cs , cslen );
		httpHeaderCharset = charset;
		memcpy(mime_charset, cs, cslen);
		mime_charset[cslen] = '\0';
		mime_charset_ptr = mime_charset;
	}

	// look for Unicode BOM first though
	cs = ucDetectBOM ( pstart , pend - pstart );
	if (cs) {
		log(LOG_DEBUG, "build: Unicode BOM signature detected: %s", cs);
		int32_t len = strlen(cs);
		if (len > 31) len = 31;
		unicodeBOMCharset = get_iana_charset(cs, len);

		if (charset == csUnknown) {
			charset = unicodeBOMCharset;
		}
	}

	// prepare to scan doc
	const char *p = pstart;
	char meta_charset[32];
	const char *meta_charset_ptr = NULL;

	//
	// it is inefficient to set xml just to get the charset.
	// so let's put in some quick string matching for this!
	//

	// advance a bit, we are initially looking for the = sign
	if ( p ) p += 10;
	// begin the string matching loop
	for ( ; p < pend ; p++ ) {
		// base everything off the equal sign
		if ( *p != '=' ) continue;
		// must have a 't' or 'g' before the equal sign
		char c = to_lower_a(p[-1]);
		// did we match "charset="?
		if ( c == 't' ) {
			if ( to_lower_a(p[-2]) != 'e' ||
			     to_lower_a(p[-3]) != 's' ||
			     to_lower_a(p[-4]) != 'r' ||
			     to_lower_a(p[-5]) != 'a' ||
			     to_lower_a(p[-6]) != 'h' ||
			     to_lower_a(p[-7]) != 'c' ) continue;
		}
			// did we match "encoding="?
		else if ( c == 'g' ) {
			if ( to_lower_a(p[-2]) != 'n' ||
			     to_lower_a(p[-3]) != 'i' ||
			     to_lower_a(p[-4]) != 'd' ||
			     to_lower_a(p[-5]) != 'o' ||
			     to_lower_a(p[-6]) != 'c' ||
			     to_lower_a(p[-7]) != 'n' ||
			     to_lower_a(p[-8]) != 'e' ) continue;
		}
			// if not either, go to next char
		else
			continue;
		// . make sure a <xml or a <meta preceeds us
		// . do not look back more than 500 chars
		const char *limit = p - 500;
		// assume charset= or encoding= did NOT occur in a tag
		bool inTag = false;
		if ( limit <  pstart ) limit = pstart;
		for ( const char *s = p ; s >= limit ; s -= 1 ) { // oneChar ) {
			// break at > or <
			if ( *s == '>' ) break;
			if ( *s != '<' ) continue;
			// . TODO: this could be in a quoted string too! fix!!
			// . is it in a <meta> tag?
			if ( to_lower_a(s[1]) == 'm' &&
			     to_lower_a(s[2]) == 'e' &&
			     to_lower_a(s[3]) == 't' &&
			     to_lower_a(s[4]) == 'a' ) {
				inTag = true;
				break;
			}
			// is it in an <xml> tag?
			if ( to_lower_a(s[1]) == 'x' &&
			     to_lower_a(s[2]) == 'm' &&
			     to_lower_a(s[3]) == 'l' ) {
				inTag = true;
				break;
			}
			// is it in an <?xml> tag?
			if ( to_lower_a(s[1]) == '?' &&
			     to_lower_a(s[2]) == 'x' &&
			     to_lower_a(s[3]) == 'm' &&
			     to_lower_a(s[4]) == 'l' ) {
				inTag = true;
				break;
			}
		}
		// if not in a tag proper, it is useless
		if ( ! inTag ) continue;
		// skip over equal sign
		p += 1;//oneChar;
		// skip over ' or "
		if ( *p == '\'' ) p += 1;//oneChar;
		if ( *p == '\"' ) p += 1;//oneChar;
		// keep start ptr
		const char *csString = p;
		// set a limit
		limit = p + 50;
		if ( limit > pend ) limit = pend;
		if ( limit < p    ) limit = pend;
		// stop at first special character
		while ( p < limit &&
		        *p &&
		        *p !='\"' &&
		        *p !='\'' &&
		        ! is_wspace_a(*p) &&
		        *p !='>' &&
		        *p != '<' &&
		        *p !='?' &&
		        *p !='/' &&
		        // fix yaya.pro-street.us which has
		        // charset=windows-1251;charset=windows-1"
		        *p !=';' &&
		        *p !='\\' )
			p += 1;//oneChar;
		size_t csStringLen = (size_t)(p-csString);
		if(csStringLen<sizeof(meta_charset)-1) {
			memcpy(meta_charset,csString,csStringLen);
			meta_charset[csStringLen] = '\0';
			meta_charset_ptr = meta_charset;
			// get the character set
			metaCharset = get_iana_charset(csString, csStringLen);
			// update "charset" to "metaCs" if known, it overrides all
			if (metaCharset != csUnknown ) {
				charset = metaCharset;
				break;
			}
		}
	}

	//At 2012 and later more than 65% of all web pages are UTF-8, so chances are that the web page is ut8 (or its subset ascii).
	//If mime or meta claims it is utf8, or if we have no hints, thne check if it is valid utf8.
	if (charset == csUTF8 || charset==csUnknown) {
		char size;
		for ( const char *s = pstart ; s < pend ; s += size ) {
			if (!isFirstUtf8Char(s)) {
				// note it
				log(LOG_DEBUG, "build: says UTF8 but does not seem to be for url %s", url);
				charset = csUnknown;
				invalidUtf8Encoding = true;
				break;
			}
			size = getUtf8CharSize(s);
		}
	}

	bool is_reliable = false;

	int bytes_consumed;
	Encoding encoding = CompactEncDet::DetectEncoding(s, slen,
		                                          url, mime_charset_ptr, meta_charset_ptr,
		                                          UNKNOWN_ENCODING, UNKNOWN_LANGUAGE,
		                                          CompactEncDet::WEB_CORPUS, false,
		                                          &bytes_consumed, &is_reliable);
	Encoding encoding2 = CompactEncDet::DetectEncoding(s, slen,
		                                           nullptr, nullptr, nullptr,
		                                           UNKNOWN_ENCODING, UNKNOWN_LANGUAGE,
		                                           CompactEncDet::WEB_CORPUS, false,
		                                           &bytes_consumed, &is_reliable);
	if(encoding!=encoding2) {
		log(LOG_INFO,"encoding: difference with/without mime/meta hints: mime_charset='%s', meta_chaset='%s', encoding1=%d, encoding2=%d, url=%s", mime_charset_ptr, meta_charset_ptr, (int)encoding, (int)encoding2, url);
	}
	
	int16_t cedCharset = convertEncodingCED(encoding);
	const char *cedCharsetStr = EncodingName(encoding);

	if (cedCharset != csUnknown && (is_reliable || charset == csUnknown)) {
		if((charset==csUTF8 || charset==csUnknown) && !invalidUtf8Encoding &&
		   (cedCharset==cswindows1250 ||
		    cedCharset==cswindows1251 ||
		    cedCharset==cswindows1252 ||
		    cedCharset==cswindows1253 ||
		    cedCharset==cswindows1254 ||
		    cedCharset==cswindows1255 ||
		    cedCharset==cswindows1256 ||
		    cedCharset==cswindows1257))
		{
			//For short documents the ced library has a weird builtin bias for classifying utf8 as windows-xxxx codepages.
			//Passing the http-header-char and meta-charset hints to ced makes it no longer recognize big5 reliably on
			//incorrectly configured webservers. So if the webserver claimed it was utf8, and we could not find any
			//illegal utf8 sequences in it and ced thinks it is a windows-xxxx codepage then we treat it as utf8.
			charset = csUTF8;
		} else {
			// we'll always use cedCharset when it's reliable or when charset is not already known
			charset = cedCharset;
		}
	}

	// alias these charsets so iconv understands
	if (charset == csISO58GB231280 || charset == csHZGB2312 || charset == csGB2312) {
		charset = csGB18030;
	}

	if (charset == csEUCKR) {
		charset = csKSC56011987; //x-windows-949
	}

	// if we're still unknown at this point, default to original behaviour
	bool defaultLatin1 = false;
	if (charset == csUnknown) {
		charset = csISOLatin1;
		defaultLatin1 = true;
	}

	log(LOG_INFO, "encoding: header-charset=%7s, bom=%7s, meta-charset=%7s; ced-charset=%15s, cedOri=%12s, is_reliable=%s, invalid=%d, defaultLatin1=%d; returned charset='%s', url='%s'",
	    get_charset_str(httpHeaderCharset), get_charset_str(unicodeBOMCharset), get_charset_str(metaCharset),
	    get_charset_str(cedCharset), cedCharsetStr, is_reliable?"true":"false", invalidUtf8Encoding, defaultLatin1,
	    get_charset_str(charset),
	    url);

	// all done
	return charset;
}
