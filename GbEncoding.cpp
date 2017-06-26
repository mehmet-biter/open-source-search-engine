#include "GbEncoding.h"
#include "HttpMime.h"
#include "iana_charset.h"
#include "Unicode.h"
#include "fctypes.h"

#include "Log.h"
#include <cstdlib>

#include "third-party/compact_enc_det/compact_enc_det/compact_enc_det.h"

uint16_t GbEncoding::getCharset(HttpMime *mime, const char *url, const char *s, int32_t slen) {
	int16_t httpHeaderCharset = csUnknown;
	int16_t unicodeBOMCharset = csUnknown;
	int16_t metaCharset = csUnknown;
	int16_t cedCharset = csUnknown;
	bool invalidUtf8Encoding = false;

	int16_t charset = csUnknown;

	if ( slen < 0 ) slen = 0;

	const char *pstart = s;
	const char *pend   = s + slen;

	const char *cs    = mime->getCharset();
	int32_t  cslen = mime->getCharsetLen();
	if ( cslen > 31 ) cslen = 31;
	if ( cs && cslen > 0 ) {
		charset = get_iana_charset ( cs , cslen );
		httpHeaderCharset = charset;
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
		// get the character set
		metaCharset = get_iana_charset(csString, csStringLen);
		// update "charset" to "metaCs" if known, it overrides all
		if (metaCharset != csUnknown ) {
			charset = metaCharset;
			break;
		}
	}

	// once again, if the doc is claiming utf8 let's double check it!
	if (charset == csUTF8) {
		// use this for iterating
		char size;
		// loop over every char
		for ( const char *s = pstart ; s < pend ; s += size ) {
			// set
			size = getUtf8CharSize(s);
			// sanity check
			if (!isFirstUtf8Char(s)) {
				// note it
				log(LOG_DEBUG, "build: says UTF8 but does not seem to be for url %s", url);
				charset = csUnknown;
				invalidUtf8Encoding = true;
				break;
			}
		}
	}

	const char *cedCharsetStr = NULL;
	bool is_reliable = false;

	int bytes_consumed;
	Encoding encoding = CompactEncDet::DetectEncoding(s, slen,
		//url, get_charset_str(httpHeaderCharset), get_charset_str(metaCharset),
		                                              nullptr, nullptr, nullptr,
		                                              UNKNOWN_ENCODING, UNKNOWN_LANGUAGE,
		                                              CompactEncDet::WEB_CORPUS, false,
		                                              &bytes_consumed, &is_reliable);

	switch (encoding) {
		case ISO_8859_1:
			cedCharset = csISOLatin1;
			break;
		case ISO_8859_2:
			cedCharset = csISOLatin2;
			break;
		case ISO_8859_3:
			cedCharset = csISOLatin3;
			break;
		case ISO_8859_4:
			cedCharset = csISOLatin4;
			break;
		case ISO_8859_5:
			cedCharset = csISOLatinCyrillic;
			break;
		case ISO_8859_6:
			cedCharset = csISOLatinArabic;
			break;
		case ISO_8859_7:
			cedCharset = csISOLatinGreek;
			break;
		case ISO_8859_8:
			cedCharset = csISOLatinHebrew;
			break;
		case ISO_8859_9:
			cedCharset = csISOLatin5;
			break;
		case ISO_8859_10:
			cedCharset = cslatin6;
			break;
		case JAPANESE_EUC_JP:
			cedCharset = csEUCJP;
			break;
		case JAPANESE_SHIFT_JIS:
			cedCharset = csxsjis;
			break;
		case JAPANESE_JIS:
			cedCharset = csJISEncoding;
			break;
		case CHINESE_BIG5:
			cedCharset = csBig5;
			break;
		case CHINESE_GB:
			cedCharset = csISO58GB231280;
			break;

//		case CHINESE_EUC_CN:       = 15,  // Misnamed. Should be EUC_TW

		case KOREAN_EUC_KR:
			cedCharset = csEUCKR;
			break;
		case UNICODE:
			cedCharset = csUnicode;
			break;

//		case CHINESE_EUC_DEC:      = 18,  // Misnamed. Should be EUC_TW
//		case CHINESE_CNS:          = 19,  // Misnamed. Should be EUC_TW
//		case CHINESE_BIG5_CP950:   = 20,  // Teragram BIG5_CP950
//		case JAPANESE_CP932:       = 21,  // Teragram CP932

		case UTF8:
			cedCharset = csUTF8;
			break;
		case UNKNOWN_ENCODING:
			cedCharset = csUnknown;
			break;
		case ASCII_7BIT:
			cedCharset = csASCII;
			break;
		case RUSSIAN_KOI8_R:
			cedCharset = csKOI8R;
			break;
		case RUSSIAN_CP1251:
			cedCharset = cswindows1251;
			break;
		case MSFT_CP1252:
			cedCharset = cswindows1252;
			break;
		case RUSSIAN_KOI8_RU:
			cedCharset = csKOI8U;
			break;
		case MSFT_CP1250:
			cedCharset = cswindows1250;
			break;
		case ISO_8859_15:
			cedCharset = csISO885915;
			break;
		case MSFT_CP1254:
			cedCharset = cswindows1254;
			break;
		case MSFT_CP1257:
			cedCharset = cswindows1257;
			break;
		case ISO_8859_11:
		case MSFT_CP874:
			cedCharset = csTIS620;
			break;
		case MSFT_CP1256:
			cedCharset = cswindows1256;
			break;
		case MSFT_CP1255:
			cedCharset = cswindows1255;
			break;
		case ISO_8859_8_I:
			cedCharset = csISO88598I;
			break;
		case HEBREW_VISUAL:
			cedCharset = csISOLatinHebrew;
			break;
		case CZECH_CP852:
			cedCharset = csPCp852;
			break;
		case CZECH_CSN_369103:
			cedCharset = csISO139CSN369103;
			break;
		case MSFT_CP1253:
			cedCharset = cswindows1253;
			break;
		case RUSSIAN_CP866:
			cedCharset = csIBM866;
			break;
		case ISO_8859_13:
			cedCharset = csISO885913;
			break;
		case ISO_2022_KR:
			cedCharset = csISO2022KR;
			break;
		case GBK:
			cedCharset = csGBK;
			break;
		case GB18030:
			cedCharset = csGB18030;
			break;
		case BIG5_HKSCS:
			cedCharset = csBig5HKSCS;
			break;
		case ISO_2022_CN:
			cedCharset = csISO2022CN;
			break;

//		case TSCII                = 49,
//		case TAMIL_MONO           = 50,
//		case TAMIL_BI             = 51,
//		case JAGRAN               = 52,

		case MACINTOSH_ROMAN:
			cedCharset = csMacintosh;
			break;
		case UTF7:
			cedCharset = csUTF7;
			break;

//		case BHASKAR              = 55,  // Indic encoding - Devanagari
//		case HTCHANAKYA           = 56,  // 56 Indic encoding - Devanagari

		case UTF16BE:
			cedCharset = csUTF16BE;
			break;
		case UTF16LE:
			cedCharset = csUTF16LE;
			break;
		case UTF32BE:
			cedCharset = csUTF32BE;
			break;
		case UTF32LE:
			cedCharset = csUTF32LE;
			break;

//		case BINARYENC            = 61,

		case HZ_GB_2312:
			cedCharset = csHZGB2312;
			break;

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
			cedCharset = csUnknown;
			break;
	}

	cedCharsetStr = EncodingName(encoding);

	if (charset == csUnknown && cedCharset != csUnknown) {
		charset = cedCharset;
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

	log(LOG_INFO, "encoding: charset='%s' header='%s' bom='%s' meta='%s' ced='%s' cedOri='%s' is_reliable=%d invalid=%d defaultLatin1=%d url='%s'",
	    get_charset_str(charset), get_charset_str(httpHeaderCharset), get_charset_str(unicodeBOMCharset),
	    get_charset_str(metaCharset), get_charset_str(cedCharset), cedCharsetStr, is_reliable, invalidUtf8Encoding, defaultLatin1, url);

	// all done
	return charset;
}