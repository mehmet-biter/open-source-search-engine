//-*- coding: utf-8 -*-

#include "gb-include.h"

#include "XmlDoc.h"
#include "CountryCode.h" // g_countryCode
#include "Collectiondb.h"
#include "Speller.h"
#include "Synonyms.h"
#include "Process.h"
#include "ip.h"
#include "Posdb.h"
#include "Conf.h"
#include "UrlBlockCheck.h"
#include "Domains.h"
#include "FxExplicitKeywords.h"
#include <algorithm>
#include "Lemma.h"
#include <unordered_set>
#include <string>


#ifdef _VALGRIND_
#include <valgrind/memcheck.h>
#endif


static void possiblyDecodeHtmlEntitiesAgain(const char **s, int32_t *len, SafeBuf *sb, bool also_remove_certain_html_elements) {
	//some documents have incorrectly encoded html entities twice. Example:
	//correct:   <meta name="foo" content="&#66;oa">
	//incorrect: <meta name="foo" content="&amp;#66;oa">
	//If it seems likely that this has happened then we decode the entities again and put the result in 'sb' and update '*s' and '*len'
	
	//Due to the (il)logic of GB the correct form is decoded, while the incorrect form is still raw, needing double decoding
	
	//require &amp; following by a second semicolon
	const char *amppos = (const char*)memmem(*s,*len, "&amp;", 5);
	if((amppos && memchr(amppos+5, ';', *len-(amppos-*s)-5)!=NULL) ||
	   (memmem(*s,*len,"&lt;",4)!=NULL && memmem(*s,*len,"&gt;",4)!=NULL)) {
		//shortest entity is 4 char (&lt;), longest utf8 encoding of a codepoint is 4 + a bit
		StackBuf<1024> tmpBuf;
		if(!tmpBuf.reserve(*len + *len/2 + 4))
			return;
		if(!sb->reserve(*len + *len/2 + 4))
			return;
		
		int32_t tmpLen = htmlDecode(tmpBuf.getBufStart(), *s,*len, false);
		
		int32_t newlen = htmlDecode(sb->getBufStart(), tmpBuf.getBufStart(), tmpLen, false);
		
		sb->setLength(newlen);
		
		//Furthermore, some websites have junk in their meta tags. Eg <br> in the meta description
		//We don't fix all cases as that could hurt correctly written pages about how to write proper html. But
		//if they don't mention "html", "tag" nor "element" then we remove the most common offenders br/b/i/p
		//When changing this function consider keeping in sync with Summary::maybeRemoveHtmlFormatting()
		if(also_remove_certain_html_elements) {
			if(memmem(sb->getBufStart(),sb->length(),"html",4)==0 &&
			   memmem(sb->getBufStart(),sb->length(),"HTML",4)==0 &&
			   memmem(sb->getBufStart(),sb->length(),"tag",3)==0 &&
			   memmem(sb->getBufStart(),sb->length(),"Tag",3)==0 &&
			   memmem(sb->getBufStart(),sb->length(),"element",7)==0 &&
			   memmem(sb->getBufStart(),sb->length(),"Element",7)==0)
			{
				sb->safeReplace2("<br>",4," ",1,0);
				sb->safeReplace2("<b>",3,"",0,0);
				sb->safeReplace2("<u>",3,"",0,0);
				sb->safeReplace2("<p>",3," ",1,0);
			}
		}
		*s = sb->getBufStart();
		*len = sb->length();
	   }
}



// a ptr to HashInfo is passed to hashString() and hashWords()
class HashInfo {
public:
	HashInfo() { 
		m_tt					= NULL;
		m_prefix				= NULL;
		m_desc					= NULL;
		m_date					= 0;
		// should we do sharding based on termid and not the usual docid???
		// in general this is false, but for checksum we want to shard
		// by the checksum and not docid to avoid having to do a 
		// gbchecksum:xxxxx search on ALL shards. much more efficient.
		m_shardByTermId 		= false;
		m_hashGroup				= -1;
		m_useCountTable			= true;
		m_useSections			= true;
		m_startDist				= 0;

		m_hashNumbers			= true;
		m_filterUrlIndexableWords	= false;
		m_linkerSiteRank		= 0;
	}
	class HashTableX *m_tt;
	const char		*m_prefix;
	// "m_desc" should detail the algorithm
	const char		*m_desc;
	int32_t			m_date;
	bool			m_shardByTermId;
	char			m_linkerSiteRank;
	char			m_hashGroup;
	int32_t			m_startDist;
	bool			m_useCountTable;
	bool			m_useSections;
	bool			m_hashNumbers;
	bool			m_filterUrlIndexableWords; //Do special filtering on words in url, eg. exclude "com" before path
};



static bool storeTerm ( const char	*s        ,
		 int32_t        slen     ,
		 int64_t		termId   ,
		 HashInfo		*hi       ,
		 int32_t        wordNum  ,
		 int32_t        wordPos  ,
		 char        densityRank,
		 char        diversityRank ,
		 char        wordSpamRank ,
		 char        hashGroup,
		 //bool        isPhrase ,
		 SafeBuf    *wbuf     ,
		 HashTableX *wts      ,
		 char        synSrc   ,
		 char        langId ,
		 posdbkey_t key ) {

	// store prefix
	int32_t poff = wbuf->length();
	// shortcut
	const char *p = hi->m_prefix;
	// add the prefix too!
	if ( p  && ! wbuf->safeMemcpy(p,strlen(p)+1)) return false;
	// none?
	if ( ! p ) poff = -1;


	// store description
	int32_t doff = wbuf->length();
	// shortcut
	const char *d = hi->m_desc;
	// add the desc too!
	if ( d && ! wbuf->safeMemcpy(d,strlen(d)+1) ) return false;
	// none?
	if ( ! d ) doff = -1;

	// store term
	int32_t toff = wbuf->length();
	// add it
	if ( ! wbuf->safeMemcpy ( s , slen ) ) return false;
	// make this
	TermDebugInfo ti;
	ti.m_termOff   = toff;
	ti.m_termLen   = slen;
	ti.m_descOff   = doff;
	ti.m_prefixOff = poff;
	ti.m_date      = hi->m_date;
	ti.m_shardByTermId = hi->m_shardByTermId;
	ti.m_termId    = termId;
	//ti.m_weight    = 1.0;
	//ti.m_spam    = -1.0;
	ti.m_diversityRank = diversityRank;
	ti.m_densityRank   = densityRank;
	ti.m_wordSpamRank  = wordSpamRank;
	ti.m_hashGroup     = hashGroup;
	ti.m_wordNum   = wordNum;
	ti.m_wordPos   = wordPos;
	ti.m_langId = langId;
	ti.m_key   = key;

	// save for printing out an asterisk
	ti.m_synSrc = synSrc; // isSynonym = isSynonym;

	// get language bit vec
	ti.m_langBitVec64 = g_speller.getLangBits64(termId);

	// make the key
	key96_t k;
	k.n1 = 0; // date
	k.n0 = termId;

	// store it
	return wts->addKey ( &k , &ti ) ;
}





//
// . hash terms that are sharded by TERMID not DOCID!!
//
// . returns false and sets g_errno on error
// . these terms are stored in indexdb, but all terms with the same
//   termId reside in one and only one group. whereas normally the records
//   are split based on docid and every group gets 1/nth of the termlist.
// . we do this "no splitting" so that only one disk seek is required, and
//   we know the termlist is small, or the termlist is being used for spidering
//   or parsing purposes and is usually not sent across the network.
bool XmlDoc::hashNoSplit ( HashTableX *tt ) {
	// constructor should set to defaults automatically
	HashInfo hi;
	hi.m_hashGroup = HASHGROUP_INTAG;
	hi.m_tt        = tt;
	// usually we shard by docid, but these are terms we shard by termid!
	hi.m_shardByTermId   = true;

	if ((size_utf8Content - 1) > 0) {
		// for exact content deduping
		setStatus("hashing gbcontenthash (deduping) no-split keys");

		// this should be ready to go and not block!
		int64_t *pch64 = getExactContentHash64();
		if (!pch64 || pch64 == (void *)-1) { g_process.shutdownAbort(true); }

		char cbuf[64];
		int32_t clen = sprintf(cbuf, "%" PRIu64, (uint64_t)*pch64);
		hi.m_prefix = "gbcontenthash";
		if (!hashString(cbuf, clen, &hi)) return false;
	}

	// now hash the site
	setStatus ( "hashing no-split SiteGetter terms");

	Url *fu = getFirstUrl();
	char *host = fu->getHost    ();

	//
	// HASH terms for SiteGetter.cpp
	//
	// these are now no-split terms
	//
	char *s    = fu->getUrl   ();
	int32_t  slen = fu->getUrlLen();
	// . this termId is used by SiteGetter.cpp for determining subsites
	// . matches what is in SiteGet::getSiteList()
	// for www.xyz.com/a/     HASH www.xyz.com
	// for www.xyz.com/a/b/   HASH www.xyz.com/a/
	// for www.xyz.com/a/b/c/ HASH www.xyz.com/a/b/
	bool  add  = true;
	// we only hash this for urls that end in '/'
	if ( s[slen-1] != '/' ) add = false;
	// and no cgi
	if ( fu->isCgi()     ) add = false;
	// skip if root
	if ( fu->getPathLen() <= 1 ) add = false;
	// sanity check
	if ( ! m_linksValid ) { g_process.shutdownAbort(true); }
	// . skip if we have no subdirectory outlinks
	// . that way we do not confuse all the pages in dictionary.com or
	//   wikipedia.org as subsites!!
	if ( ! m_links.hasSubdirOutlink() ) add = false;
	// hash it
	if ( add ) {
		// remove the last path component
		char *end2 = s + slen - 2;
		// back up over last component
		for ( ; end2 > fu->getPath() && *end2 != '/' ; end2-- ) ;
		// hash that part of the url
		hi.m_prefix    = "siteterm";
		if ( ! hashSingleTerm ( host,end2-host,&hi) ) return false;
	}

	return true;
}

// . returns -1 if blocked, returns NULL and sets g_errno on error
// . "sr" is the tagdb Record
// . "ws" store the terms for PageParser.cpp display
char *XmlDoc::hashAll(HashTableX *table) {
	logTrace(g_conf.m_logTraceXmlDoc, "BEGIN");

	setStatus("hashing document");

	if (m_allHashed) {
		return (char *)1;
	}

	// sanity checks
	if (table->getKeySize() != 18 || table->getDataSize() != 4) {
		g_process.shutdownAbort(true);
	}

	// ptr to term = 4 + score = 4 + ptr to sec = 4
	if (m_wts && (m_wts->getKeySize() != 12 || m_wts->getDataSize() != sizeof(TermDebugInfo))) {
		g_process.shutdownAbort(true);
	}

	uint8_t *ct = getContentType();
	if (!ct) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getContentType failed");
		return NULL;
	}
	
	lemma_words.clear();

	// BR 20160127: Never index JSON and XML content
	if (*ct == CT_JSON || *ct == CT_XML) {
		// For XML (JSON should not get here as it should be filtered out during spidering)
		// store the URL as the only thing in posdb so we are able to find it, and
		// eventually ban it.
		if (!hashUrl(table, true)) {  // urlOnly (skip IP and term generation)
			logTrace(g_conf.m_logTraceXmlDoc, "END, hashUrl failed");
			return NULL;
		}
		m_allHashed = true;
		return (char *)1;
	}

	// need this for hashing
	HashTableX *cnt = getCountTable();
	if (!cnt) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getCountTable failed");
		return (char *)cnt;
	}
	if (cnt == (void *)-1) {
		g_process.shutdownAbort(true);
	}

	// and this
	Links *links = getLinks();
	if (!links) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getLinks failed");
		return (char *)links;
	}
	if (links == (Links *)-1) {
		g_process.shutdownAbort(true);
	}

	char *wordSpamVec = getWordSpamVec();
	if (!wordSpamVec) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getWordSpamVec failed");
		return wordSpamVec;
	}
	if (wordSpamVec == (void *)-1) {
		g_process.shutdownAbort(true);
	}

	char *fragVec = getFragVec();
	if (!fragVec) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getFragVec failed");
		return fragVec;
	}
	if (fragVec == (void *)-1) {
		g_process.shutdownAbort(true);
	}

	// why do we need this?
	if ( m_wts ) {
		uint8_t *lv = getLangVector();
		if (!lv) {
			logTrace(g_conf.m_logTraceXmlDoc, "END, getLangVector failed");
			return (char *)lv;
		}
		if (lv == (void *)-1) {
			g_process.shutdownAbort(true);
		}
	}

	CollectionRec *cr = getCollRec();
	if ( ! cr ) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, getCollRec failed");
		return NULL;
	}

	// do not repeat this if the cachedb storage call blocks
	m_allHashed = true;

	// reset distance cursor
	m_dist = 0;

	if (!hashContentType(table)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashContentType failed");
		return NULL;
	}

	if (!hashUrl(table, false)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashUrl failed");
		return NULL;
	}

	if (!hashLanguage(table)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashLanguage failed");
		return NULL;
	}

	if (!hashCountry(table)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashCountry failed");
		return NULL;
	}

	// now hash the terms sharded by termid and not docid here since they
	// just set a special bit in posdb key so Rebalance.cpp can work.
	// this will hash the content checksum which we need for deduping
	// which we use for diffbot custom crawls as well.
	if (!hashNoSplit(table)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashNoSplit failed");
		return NULL;
	}

	// MDW: i think we just inject empty html with a diffbotreply into
	// global index now, so don't need this... 9/28/2014

	// stop indexing xml docs
	// global index unless this is a json object in which case it is
	// hashed above in the call to hashJSON(). this will decrease disk
	// usage by about half, posdb* files are pretty big.
	if (!cr->m_indexBody) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, !indexDoc");
		return (char *)1;
	}

	bool *ini = getIsNoIndex();
	if (ini == nullptr || ini == (bool*)-1) {
		// must not be blocked
		gbshutdownLogicError();
	}

	if (*ini && m_version > 126) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, noindex");
		return (char *)1;
	}

	if ((size_utf8Content - 1) <= 0) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, contentLen == 0");
		return (char *)1;
	}

	// hash the body of the doc first so m_dist is 0 to match
	// the rainbow display of sections
	if (!hashBody2(table)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashBody2 failed");
		return NULL;
	}

	// hash the title now too so neighborhood singles have more
	// to match. plus, we only hash these title terms iff they
	// are not already in the hash table, so as to avoid hashing
	// repeated title terms because we do not do spam detection
	// on them. thus, we need to hash these first before anything
	// else. give them triple the body score
	if (!hashTitle(table)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashTitle failed");
		return NULL;
	}

	// . hash the keywords tag, limited to first 2k of them so far
	// . hash above the neighborhoods so the neighborhoods only index
	//   what is already in the hash table
	if (!hashMetaKeywords(table)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashMetaKeywords failed");
		return NULL;
	}

	//Hash explicit keywords, if any
	if(!hashExplicitKeywords(table)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashExplicityKeywords failed");
		return NULL;
	}

	// then hash the incoming link text, NO ANOMALIES, because
	// we index the single words in the neighborhoods next, and
	// we had songfacts.com coming up for the 'street light facts'
	// query because it had a bunch of anomalous inlink text.
	if (!hashIncomingLinkText(table)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashIncomingLinkText failed");
		return NULL;
	}

	// then the meta summary and description tags with half the score of
	// the body, and only hash a term if was not already hashed above
	// somewhere.
	if (!hashMetaSummary(table)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashMetaSummary failed");
		return NULL;
	}

	// BR 20160220
	// Store value of meta tag "geo.placename" to help aid searches for
	// location specific sites, e.g. 'Restaurant in London'
	if (!hashMetaGeoPlacename(table)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashMetaGeoPlacename failed");
		return NULL;
	}

	// this will only increment the scores of terms already in the table
	// because we neighborhoods are not techincally in the document
	// necessarily and we do not want to ruin our precision
	if (!hashNeighborhoods(table)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashNeighborhoods failed");
		return NULL;
	}

	if (!hashLinks(table)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashLinks failed");
		return NULL;
	}

	if (!hashMetaTags(table)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashMetaTags failed");
		return NULL;
	}

	// hash gblang:de last for parsing consistency
	if (!hashLanguageString(table)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashLanguageString failed");
		return NULL;
	}

	if(!hashLemmas(table)) {
		logTrace(g_conf.m_logTraceXmlDoc, "END, hashLemmas failed");
		return NULL;
	}
	lemma_words.clear(); //release memory early

	logTrace(g_conf.m_logTraceXmlDoc, "END, OK");
	return (char *)1;
}

// returns false and sets g_errno on error
bool XmlDoc::hashMetaTags ( HashTableX *tt ) {

	setStatus ( "hashing meta tags" );

	int32_t     n     = m_xml.getNumNodes();
	XmlNode *nodes = m_xml.getNodes();

	// set up the hashing parms
	HashInfo hi;
	hi.m_hashGroup = HASHGROUP_INMETATAG;
	hi.m_tt        = tt;
	hi.m_desc      = "custom meta tag";

	// find the first meta summary node
	for ( int32_t i = 0 ; i < n ; i++ ) {
		//we are only interested in meta tags
		if(nodes[i].m_nodeId != TAG_META)
			continue;
		// only get content for <meta name=..> not <meta http-equiv=..>
		int32_t tagLen;
		const char *tag = m_xml.getString(i, "name", &tagLen);
		// skip if error/empty
		if ( ! tag || tagLen <= 0 ) continue;

		// this is now reserved
		// do not hash keyword, keywords, description, or summary metas
		// because that is done in hashRange() below based on the
		// tagdb (ruleset) record
		if ((tagLen== 7&&strncasecmp(tag,"keyword"    , 7)== 0)||
		    (tagLen== 7&&strncasecmp(tag,"summary"    , 7)== 0)||
		    (tagLen== 8&&strncasecmp(tag,"keywords"   , 8)== 0)||
		    (tagLen==11&&strncasecmp(tag,"description",11)== 0) )
			continue;


		// BR 20160107: Only hash certain custom meta tags and ignore the rest
		if(
			(strncasecmp(tag,"subject", 7) != 0) &&
			(strncasecmp(tag,"abstract", 8) != 0) &&
			(strncasecmp(tag,"news_keywords", 13) != 0) &&		// http://www.metatags.org/meta_name_news_keywords
			(strncasecmp(tag,"author", 6) != 0) &&
			(strncasecmp(tag,"title", 5) != 0) &&
			(strncasecmp(tag,"og:title", 8) != 0) &&
			(strncasecmp(tag,"og:description", 14) != 0) &&
			(strncasecmp(tag,"twitter:title", 13) != 0) &&
			(strncasecmp(tag,"twitter:description", 19) != 0) )
		{
			// If none of the above, it is an unwanted meta tag
			continue;
		}

		// get the content
		int32_t len;
		const char *s = m_xml.getString ( i , "content" , &len );
		if ( ! s || len <= 0 ) continue;

		StackBuf<1024> doubleDecodedContent;
		possiblyDecodeHtmlEntitiesAgain(&s, &len, &doubleDecodedContent, true);

		// Now index the wanted meta tags as normal text without prefix so they
		// are used in user searches automatically.
		hi.m_prefix = NULL;

		bool status = hashString4(s,len,&hi);

		// bail on error, g_errno should be set
		if ( ! status ) return false;
	}

	return true;
}



// returns false and sets g_errno on error
bool XmlDoc::hashContentType ( HashTableX *tt ) {

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return false;


	uint8_t *ctype = getContentType();
	if( !ctype ) {
		return false;
	}

	const char *s = NULL;

	setStatus ( "hashing content type" );


	// hash numerically so we can do gbfacetint:type on it
	HashInfo hi;
	hi.m_hashGroup = HASHGROUP_INTAG;
	hi.m_tt        = tt;
	hi.m_prefix    = "type";

	char tmp[6];
	sprintf(tmp,"%" PRIu32,(uint32_t)*ctype);
	if ( ! hashString (tmp,strlen(tmp),&hi ) ) return false;


	// these ctypes are defined in HttpMime.h
	switch (*ctype) {
		case CT_HTML: s = "html"; break;
		case CT_TEXT: s = "text"; break;
		case CT_XML : s = "xml" ; break;
		case CT_PDF : s = "pdf" ; break;
		case CT_DOC : s = "doc" ; break;
		case CT_XLS : s = "xls" ; break;
		case CT_PPT : s = "ppt" ; break;
		case CT_PS  : s = "ps"  ; break;
		// for diffbot. so we can limit search to json objects
		// in Diffbot.cpp
		case CT_JSON: s = "json"  ; break;
	}
	// bail if unrecognized content type
	if ( ! s ) return true;

	// . now hash it
	// . use a score of 1 for all
	// . TODO: ensure doc counting works ok with this when it does
	//   it's interpolation
	return hashString (s,strlen(s),&hi );
}

// . hash the link: terms
// . ensure that more useful linkers are scored higher
// . useful for computing offsite link text for qdb-ish algorithm
// . NOTE: for now i do not hash links to the same domain in order to
//   hopefully save 10%-25% index space
// . NOTE: PLUS, they may clog up the link-adjusted quality ratings since
//   different site links with no link text will be ranked behind them
// . the 8-bit bitmap of the score of a link: term:
// . 00ubdcss  u = link is Unbanned? b = link isBanned?
//             d = link dirty?       c = link clean?
//             s = 01 if no link text, 10 if link text
// . NOTE: this is used in Msg18.cpp for extraction
// . CAUTION: IndexList::score32to8() will warp our score if its >= 128
//   so i moved the bits down
bool XmlDoc::hashLinks ( HashTableX *tt ) {

	setStatus ( "hashing links" );

	// shortcuts
	bool isRSSFeed = *getIsRSS();

	char dbuf[8*4*1024];
	HashTableX dedup;
	dedup.set( 8,0,1024,dbuf,8*4*1024,false,"hldt");

	CollectionRec *cr = getCollRec();
	if ( ! cr ) {
		logTrace( g_conf.m_logTraceXmlDoc, "END, getCollRec failed" );
		return false;
	}

	// see ../url/Url2.cpp for hashAsLink() algorithm
	for ( int32_t i = 0 ; i < m_links.m_numLinks ; i++ ) {
		// skip links with zero 0 length
		if ( m_links.m_linkLens[i] == 0 ) {
			continue;
		}

		// . skip if we are rss page and this link is an <a href> link
		// . we only harvest/index <link> urls from rss feeds
		// . or in the case of feedburner, those orig tags
		if ( isRSSFeed && (m_links.m_linkFlags[i] & LF_AHREFTAG) ) {
			continue;
		}

		// if we have a <feedburner:origLink> tag, then ignore <link>
		// tags and only get the links from the original links
		if ( m_links.m_isFeedBurner && !(m_links.m_linkFlags[i] & LF_FBTAG) ) {
			continue;
		}

		// normalize the link
		Url link;

		// now we always add "www" to these links so that any link
		// to cnn.com is same as link to www.cnn.com, because either
		// we index cnn.com or www.cnn.com but not both providing
		// their content is identical (deduping). This way whichever
		// one we index, we can take advantage of all link text whether
		// it's to cnn.com or www.cnn.com.
		// Every now and then we add new session ids to our list in
		// Url.cpp, too, so we have to version that.
		// Since this is just for hashing, it shouldn't matter that
		// www.tmblr.co has no IP whereas only tmblr.co does.
		link.set( m_links.m_linkPtrs[i], m_links.m_linkLens[i], true, m_links.m_stripParams, m_version );

		// BR 20160105: Do not create "link:" hashes for media URLs etc.
		if( link.hasNonIndexableExtension(TITLEREC_CURRENT_VERSION) ||	// @todo BR: For now ignore actual TitleDB version. // m_version) ||
			link.hasScriptExtension() ||
			link.hasJsonExtension() ||
			link.hasXmlExtension() ||
			isUrlBlocked(link)) {
			logTrace( g_conf.m_logTraceXmlDoc, "Unwanted for indexing [%s]", link.getUrl());
			continue;			
		}

		// dedup this crap
		int64_t h = hash64 ( link.getUrl(), link.getUrlLen() );
		if ( dedup.isInTable ( &h ) ) continue;
		if ( ! dedup.addKey ( &h ) ) return false;

		// set up the hashing parms
		HashInfo hi;
		hi.m_hashGroup = HASHGROUP_INTAG;
		hi.m_tt        = tt;
		hi.m_prefix    = "link";

		// hash link:<url>
		if ( ! hashSingleTerm ( link.getUrl(),link.getUrlLen(),&hi )) {
			return false;
		}

		h = hash64 ( link.getHost() , link.getHostLen() );
		if ( dedup.isInTable ( &h ) ) continue;
		if ( ! dedup.addKey ( &h ) ) return false;

		// fix parm
		hi.m_prefix    = "sitelink";

		// hash sitelink:<urlHost>
		if ( ! hashSingleTerm ( link.getHost(),link.getHostLen(),&hi)) {
			return false;
		}
	}

	return true;
}


// . returns false and sets g_errno on error
// . hash for linkdb
bool XmlDoc::hashLinksForLinkdb ( HashTableX *dt ) {

	// sanity check
	if ( dt->getKeySize() != sizeof(key224_t) ) { g_process.shutdownAbort(true); }
	if ( dt->getDataSize() != 0                ) { g_process.shutdownAbort(true); }

	// this will be different with our new site definitions
	uint32_t linkerSiteHash32 = *getSiteHash32();

	char siteRank = getSiteRank();

	if ( ! m_linksValid ) { g_process.shutdownAbort(true); }

	int32_t *linkSiteHashes = getLinkSiteHashes();
	if ( ! linkSiteHashes || linkSiteHashes == (void *)-1 ) {
		g_process.shutdownAbort(true);
	}

	// use spidered time! might not be current time! like if rebuilding
	// or injecting from a past spider time
	int32_t discoveryDate = getSpideredTime();

	// add in new links
	for ( int32_t i = 0 ; i < m_links.m_numLinks ; i++ ) {
		// skip if empty
		if (m_links.m_linkLens[i] == 0) {
			continue;
		}

		// . skip if spam, ALWAYS allow internal outlinks though!!
		// . CAUTION: now we must version islinkspam()
		bool spam = m_links.isLinkSpam(i);

		// get site of outlink from tagrec if in there
		int32_t linkeeSiteHash32 = linkSiteHashes[i];

		//
		// when setting the links class it should set the site hash
		//


#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(&linkeeSiteHash32,sizeof(linkeeSiteHash32));
	uint64_t tmp1 = m_links.getLinkHash64(i);
	VALGRIND_CHECK_MEM_IS_DEFINED(&tmp1,sizeof(tmp1));
	VALGRIND_CHECK_MEM_IS_DEFINED(&spam,sizeof(spam));
	VALGRIND_CHECK_MEM_IS_DEFINED(&siteRank,sizeof(siteRank));
//	uint32_t tmp2 = *getIp();
//	VALGRIND_CHECK_MEM_IS_DEFINED(&tmp2,sizeof(tmp2));
	uint64_t tmp3 = *getDocId();
	VALGRIND_CHECK_MEM_IS_DEFINED(&tmp3,sizeof(tmp3));
	VALGRIND_CHECK_MEM_IS_DEFINED(&discoveryDate,sizeof(discoveryDate));
	VALGRIND_CHECK_MEM_IS_DEFINED(&linkerSiteHash32,sizeof(linkerSiteHash32));
#endif

		int32_t *ipptr = getIp();
		int32_t ip = ipptr ? *ipptr : 0;

		// set this key, it is the entire record
		key224_t k = Linkdb::makeKey_uk ( linkeeSiteHash32 ,
					  m_links.getLinkHash64(i)   ,
					  spam               , // link spam?
					  siteRank     , // was quality
					  ip,
					  *getDocId()    ,
					  discoveryDate      ,
					  0           ,
					  false              , // new add?
					  linkerSiteHash32   ,
					  false              );// delete?
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(&k,sizeof(k));
#endif

		// store in hash table
		if (!dt->addKey(&k, NULL)) {
			return false;
		}
	}
	return true;
}

// . returns false and sets g_errno on error
// . copied Url2.cpp into here basically, so we can now dump Url2.cpp
bool XmlDoc::hashUrl ( HashTableX *tt, bool urlOnly ) { // , bool isStatusDoc ) {

	setStatus ( "hashing url colon" );

	// get the first url
	Url *fu = getFirstUrl();

	// set up the hashing parms
	HashInfo hi;
	hi.m_hashGroup = HASHGROUP_INTAG;
	hi.m_tt        = tt;

	// we do not need diversity bits for this
	hi.m_useCountTable = false;
	//
	// HASH url: term
	//
	// append a "www." for doing url: searches
	Url uw;
	uw.set( fu->getUrl(), fu->getUrlLen(), true, false );
	hi.m_prefix    = "url";

	if ( ! hashSingleTerm(uw.getUrl(),uw.getUrlLen(),&hi) )
		return false;

	if (urlOnly) {
		return true;
	}

	bool *ini = getIsNoIndex();
	if (ini == nullptr || ini == (bool*)-1) {
		// must not be blocked
		gbshutdownLogicError();
	}

	const char *s = fu->getUrl();
	int32_t slen = fu->getUrlLen();

	SafeBuf sb_punyDecodedHost;
	//no-index support was added in version 126. So if noindex is not present, or if un-indexing an older titlerecversion then do the index
	if (!*ini || m_version <= 126) {
		setStatus("hashing inurl colon");

		//
		// HASH inurl: terms
		//
		hi.m_prefix = "inurl";

		// BR 20160114: Skip numbers in urls when doing "inurl:" queries
		hi.m_hashNumbers = false;
		hi.m_filterUrlIndexableWords = true;
		if (!hashString(s, slen, &hi)) return false;
		
		//If the host has punycode encoded characters in it and the TLD has some enforcement against phishing
		//and misleading domains then index the punycode-decoded string too
		if(fu->isPunycodeSafeTld() && fu->hasPunycode()) {
			if(fu->getPunycodeDecodedHost(&sb_punyDecodedHost)) {
				//note: we index non-punycode labels too, it is not worth the effort to avoid that
				//because we also need them for bigram generation. So eg www.ærtesuppe.dk will get
				//indexed for "www", "xn--rtesuppe-i0a", and "dk" in the hashStrings() call above
				//and them for "www", "ærtesuppe" and "dk" below.
				if (!hashString(sb_punyDecodedHost.getBufStart(), sb_punyDecodedHost.length(), &hi))
					return false;
			}
		}
	}

	{
		setStatus("hashing ip colon");
		hi.m_hashNumbers = true;
		hi.m_filterUrlIndexableWords = false;

		//
		// HASH ip:a.b.c.d
		//
		if (!m_ipValid) { g_process.shutdownAbort(true); }
		// copy it to save it
		char ipbuf[64];
		int32_t iplen = strlen(iptoa(m_ip, ipbuf));
		hi.m_prefix = "ip";
		if (!hashSingleTerm(ipbuf, iplen, &hi)) return false;

		// . sanity check
		if (!m_siteNumInlinksValid) { g_process.shutdownAbort(true); }
	}


	//
	// HASH the url's mid domain and host as they were in the body
	//
	setStatus ( "hashing site colon terms");

	//
	// HASH the site: terms
	//
	// . hash the pieces of the site
	// . http://host.domain.com/~harry/level1/ should hash to:
	// . site:host.domain.com/~harry/level1/
	// . site:host.domain.com/~harry/
	// . site:host.domain.com/~
	// . site:host.domain.com/
	// . site:domain.com/~harry/level1/
	// . site:domain.com/~harry/
	// . site:domain.com/~
	// . site:domain.com/
	// ensure score is positive
	//if ( siteScore <= 0 ) siteScore = 1;
	// get the hostname (later we set to domain name)
	char *name    = fu->getHost();
	int32_t  nameLen = fu->getHostLen();
	
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(name,nameLen);
#endif
	// . point to the end of the whole thing, including port field
	// . add in port, if non default
	char *end3    = name + fu->getHostLen() + fu->getPortLen();
	
	// Generate string with port if server runs on non-standard ports
	char pbuf[12];
	int pbufLen=0;
	int32_t port = fu->getPort();
	if( port > 0 && port != 80 && port != 443 ) {
		pbufLen=snprintf(pbuf, 12, ":%" PRIu32, (uint32_t)fu->getPort());
	}


 loop:
	// now loop through the sub paths of this url's path
	int32_t prev_len = -1;
	for ( int32_t i = 0 ; ; i++ ) {
		// get the subpath
		int32_t len = fu->getSubPathLen(i);
		if(len==prev_len) //work around bug (?) in Url
			continue;
		prev_len = len;

		// FIX: always include first /
		if ( len == 0 ) {
			len = 1;
		}

		// write http://www.whatever.com/path into buf
		char buf[MAX_URL_LEN+10];
		char *p = buf;
		
		// BR 20160122: Do NOT fix this for https sites. The search is
		// always prefixed with http:// (sigh ...)
		gbmemcpy ( p , "http://" , 7 ); p += 7;
		gbmemcpy ( p , name, nameLen); 	p += nameLen;
		if( pbufLen > 0 )
		{
			gbmemcpy ( p , pbuf, pbufLen); p += pbufLen;
		}	
		gbmemcpy ( p , fu->getPath() , len          ); p += len;
		*p = '\0';

		// update hash parms
		if (m_version <= 126) {
			hi.m_prefix = "site";
		} else {
			hi.m_prefix = *ini ? "sitenoindex" : "site";
		}

		hi.m_hashGroup = HASHGROUP_INURL;
		
		
		// this returns false on failure
		if ( ! hashSingleTerm (buf,p-buf,&hi ) ) {
			return false;
		}

		// break when we hash the root path
		if ( len <=1 ) {
			break;
		}
	}

	// now keep moving the period over in the hostname
	while ( name < end3 && *name != '.' ) {
		name++; 
		nameLen--; 
	}

	// skip the '.'
	name++; nameLen--;

	// Check that there is a dot before first slash after domain
	// to avoid junk entries like http://com/subpath/pagename.html
	bool dom_valid = false;
	if( nameLen > 0 )
	{
		int32_t dom_offset=0;
		if( strncmp(name,"http://" ,7)==0 )
		{
			dom_offset=7;
		}
		else
		if( strncmp(name,"https://",8)==0 )
		{
			dom_offset=8;
		}
	
		const char *dotpos 	= (const char *)memchr(name,'.',nameLen);
		const char *slashpos= (const char *)memchr(name+dom_offset,'/',nameLen-dom_offset);
	
		if( dotpos && (!slashpos || (slashpos > dotpos)) )
		{
			dom_valid = true;
		}
	}
	
	if ( name < end3 && dom_valid ) goto loop;



	// BR 20160121: Make searching for e.g. site:dk work
	setStatus ( "hashing tld for site search");
	const char *tld = fu->getTLD();
	int32_t tldLen = fu->getTLDLen();

	if( tldLen > 0 && tldLen < 64 ) {
		char tldBuf[72];	// http:// (7) + tld (63) + / (1) + 0 (1)
		char *p = tldBuf;
		gbmemcpy ( p , "http://", 7 ); p += 7;
		gbmemcpy ( p , tld, tldLen); p += tldLen;
		gbmemcpy ( p , "/", 1 ); p += 1;
		*p = '\0';
		if ( ! hashSingleTerm (tldBuf, p - tldBuf, &hi ) ) {
			return false;
		}
	}

	const char *ext = fu->getExtension();
	int32_t elen = fu->getExtensionLen();
	if (!*ini || m_version <= 126) {
		//
		// HASH ext: term
		//
		// i.e. ext:gif ext:html ext:htm ext:pdf, etc.
		setStatus("hashing ext colon");
		// update hash parms
		hi.m_prefix = "ext";
		if (!hashSingleTerm(ext, elen, &hi)) return false;
	}

	{
		setStatus("hashing gbdocid");
		hi.m_prefix = "gbdocid";
		char buf2[32];
		sprintf(buf2, "%" PRIu64, (uint64_t)m_docId);
		if (!hashSingleTerm(buf2, strlen(buf2), &hi)) return false;
	}

	setStatus ( "hashing SiteGetter terms");

	//
	// HASH terms for SiteGetter.cpp
	//
	// . this termId is used by SiteGetter.cpp for determining subsites
	// . matches what is in SiteGet::getSiteList()
	// for www.xyz.com/a/     HASH www.xyz.com
	// for www.xyz.com/a/b/   HASH www.xyz.com/a/
	// for www.xyz.com/a/b/c/ HASH www.xyz.com/a/b/
	bool  add  = true;
	// we only hash this for urls that end in '/'
	if ( s[slen-1] != '/' ) add = false;
	// and no cgi
	if ( fu->isCgi()     ) add = false;
	// skip if root
	if ( fu->getPathLen() <= 1 ) add = false;
	// sanity check
	if ( ! m_linksValid ) { g_process.shutdownAbort(true); }
	// . skip if we have no subdirectory outlinks
	// . that way we do not confuse all the pages in dictionary.com or
	//   wikipedia.org as subsites!!
	if ( ! m_links.hasSubdirOutlink() ) add = false;

	const char *host = fu->getHost();
	int32_t  hlen = fu->getHostLen     ();

	// tags from here out
	hi.m_hashGroup = HASHGROUP_INTAG;
	hi.m_shardByTermId = true;
	// hash it
	if ( add ) {
		// remove the last path component
		const char *end2 = s + slen - 2;
		// back up over last component
		for ( ; end2 > fu->getPath() && *end2 != '/' ; end2-- ) ;
		// hash that part of the url
		hi.m_prefix    = "siteterm";
		if ( ! hashSingleTerm ( host,end2-host,&hi) ) return false;
	}
	hi.m_shardByTermId  = false;

	setStatus ( "hashing urlhashdiv10 etc");

	//
	// HASH urlhash: urlhashdiv10: urlhashdiv100: terms
	//
	// this is for proving how many docs are in the index
	char buf[20];
	int32_t blen;

	uint32_t h = hash32 ( s , slen );
	blen = sprintf(buf,"%" PRIu32,h);
	hi.m_prefix    = "urlhash";
	if ( ! hashString(buf,blen,&hi) ) return false;

	// don't index mid domain or url path for noindex document
	if (*ini && m_version > 126) {
		return true;
	}

	if (size_utf8Content - 1 > 0 || m_indexCode == EDOCDISALLOWEDROOT) {
		setStatus("hashing url mid domain");

		// update parms
		hi.m_prefix = NULL;
		hi.m_desc = "middle domain";
		hi.m_hashGroup = HASHGROUP_INURL;
		hi.m_filterUrlIndexableWords = true;    // Skip com, http etc.
		if (!hashString(host, hlen, &hi)) {
			return false;
		}
		if(sb_punyDecodedHost.length()>1) {
			if(!hashString(sb_punyDecodedHost.getBufStart(),sb_punyDecodedHost.length(), &hi))
				return false;
		}

		hi.m_filterUrlIndexableWords = false;
		if (!hashSingleTerm(fu->getDomain(), fu->getDomainLen(), &hi)) {
			return false;
		}
	}

	if (size_utf8Content - 1 > 0) {
		setStatus("hashing url path");
		const char *path = fu->getPath();
		int32_t plen = fu->getPathLen();

		// BR 20160113: Do not hash and combine the page filename extension with the page name (skip e.g. .com)
		if (elen > 0) {
			elen++;    // also skip the dot
		}
		plen -= elen;

		// BR 20160113: Do not hash the most common page names
		if (strncmp(path, "/index", plen) != 0) {
			// hash the path
			// BR 20160114: Exclude numbers in paths (usually dates)
			hi.m_hashGroup = HASHGROUP_INURL;
			hi.m_hashNumbers = false;
			if (!hashString(path, plen, &hi)) return false;
		}
	}

	//actually index the middle domain. The above indexing of filtered-host and singleterm-domain was in the original code so it was always misleading
	{
		setStatus("hashing url mid domain");
		hi.m_prefix = NULL;
		hi.m_desc = "middle domain(2)";
		hi.m_hashGroup = HASHGROUP_MIDDOMAIN;
		hi.m_filterUrlIndexableWords = false;
		const char *mdom = fu->getMidDomain();
		int32_t mdomlen = fu->getMidDomainLen();
		if (!hashString(mdom, mdomlen, &hi)) {
			return false;
		}
		if(fu->isPunycodeSafeTld() && fu->hasPunycode()) {
			SafeBuf sb_punyDecodedMidDomain;
			if(fu->getPunycodeDecodedMidDomain(&sb_punyDecodedMidDomain)) {
				if (!hashString(sb_punyDecodedMidDomain.getBufStart(), sb_punyDecodedMidDomain.length(), &hi))
					return false;
			}
		}
	}
	
	return true;
}

// . returns false and sets g_errno on error
bool XmlDoc::hashIncomingLinkText(HashTableX *tt) {

	setStatus ( "hashing link text" );

	// sanity
	if ( ! m_linkInfo1Valid ) { g_process.shutdownAbort(true); }

	// . finally hash in the linkText terms from the LinkInfo
	// . the LinkInfo class has all the terms of hashed anchor text for us
	// . if we're using an old TitleRec linkTermList is just a ptr to
	//   somewhere in TitleRec
	// . otherwise, we generated it from merging a bunch of LinkInfos
	//   and storing them in this new TitleRec
	LinkInfo  *linkInfo = getLinkInfo1();

	// sanity checks
	if ( ! m_ipValid             ) { g_process.shutdownAbort(true); }
	if ( ! m_siteNumInlinksValid ) { g_process.shutdownAbort(true); }

	//
	// brought the following code in from LinkInfo.cpp
	//

	// count "external" inlinkers
	int32_t ecount = 0;

	// update hash parms
	HashInfo hi;
	hi.m_tt        = tt;
	// hashstring should update this like a cursor.
	hi.m_startDist = 0;

	// loop through the link texts and hash them
	for ( Inlink *k = NULL; linkInfo && (k = linkInfo->getNextInlink(k)) ; ) {
		// is this inlinker internal?
		bool internal=((m_ip&0x0000ffff)==(k->m_ip&0x0000ffff));
		// count external inlinks we have for indexing gbmininlinks:
		if ( ! internal ) ecount++;

		// get length of link text
		int32_t tlen = k->size_linkText;
		if ( tlen > 0 ) tlen--;
		// get the text
		const char *txt = k->getLinkText();
		// sanity check
		if ( ! verifyUtf8 ( txt , tlen ) ) {
			log("xmldoc: bad link text 2 from url=%s for %s",
			    k->getUrl(),m_firstUrl.getUrl());
			continue;
		}

		if ( internal ) hi.m_hashGroup = HASHGROUP_INTERNALINLINKTEXT;
		else            hi.m_hashGroup = HASHGROUP_INLINKTEXT;
		// store the siterank of the linker in this and use that
		// to set the multiplier M bits i guess
		hi.m_linkerSiteRank = k->m_siteRank;
		if(hi.m_linkerSiteRank>MAXSITERANK) {
			log(LOG_INFO,"Inlink had siteRank>max (%d), probably from docid %ld", k->m_siteRank, k->m_docId);
			hi.m_linkerSiteRank = MAXSITERANK;
		}
		// now record this so we can match the link text to
		// a matched offsite inlink text term in the scoring info
		k->m_wordPosStart = m_dist; // hi.m_startDist;
		// . hash the link text into the table
		// . returns false and sets g_errno on error
		// . we still have the score punish from # of words though!
		// . for inlink texts that are the same it should accumulate
		//   and use the reserved bits as a multiplier i guess...
		if ( ! hashString4(txt,tlen,&hi) ) return false;
		// now record this so we can match the link text to
		// a matched offsite inlink text term in the scoring info
		//k->m_wordPosEnd = hi.m_startDist;
		// spread it out
		hi.m_startDist += 20;
	}

	return true;
}

// . returns false and sets g_errno on error
bool XmlDoc::hashNeighborhoods ( HashTableX *tt ) {
	setStatus ( "hashing neighborhoods" );

	// . now we also hash the neighborhood text of each inlink, that is,
	//   the text surrounding the inlink text.
	// . this is also destructive in that it will remove termids that
	//   were not in the document being linked to in order to save
	//   space in the titleRec
	// . now we only do one or the other, not both
	LinkInfo  *linkInfo = getLinkInfo1();
	if(!linkInfo)
		return true;

	// loop over all the Inlinks
	for(Inlink *k = linkInfo->getNextInlink(NULL); k; k = linkInfo->getNextInlink(k)) {
		// skip if internal, they often have the same neighborhood text
		if((k->m_ip&0x0000ffff)==(m_ip&0x0000ffff))
			continue;

		// get the left and right texts and hash both
		const char *s = k->getSurroundingText();
		if(!s || k->size_surroundingText <= 1)
			continue;

		// update hash parms
		HashInfo hi;
		hi.m_tt        = tt;
		hi.m_desc      = "surrounding text";
		hi.m_hashGroup = HASHGROUP_NEIGHBORHOOD;

		// . hash that
		// . this returns false and sets g_errno on error
		int32_t len = k->size_surroundingText - 1;
		if(!hashString(s, len, &hi))
			return false;
	}
	return true;
}

// . we now do the title hashing here for newer titlerecs, version 80+, rather
//   than use the <index> block in the ruleset for titles.
// . this is not to be confused with hashing the title: terms which still
//   does have an <index> block in the ruleset.
// . the new Weights class hashes title as part of body now with a high weight
//   given by "titleWeight" parm
bool XmlDoc::hashTitle ( HashTableX *tt ) {
	// sanity check
	if ( m_hashedTitle ) { g_process.shutdownAbort(true); }

	setStatus ( "hashing title" );

	// this has been called, note it
	m_hashedTitle = true;

	//getXml()->getUtf8Content() results in the HTML to be ~mostly~ decoded but lt/gt/amp are still there escaped.
	//So get the title text from m_xml, retokenize it, and then index that
	int rawTitleLen;
	const char *rawTitle = m_xml.getString("title",&rawTitleLen);
	if(!rawTitle) {
		//no title - nothing to do
		return true;
	}
	
	//The amp/lt/gt are still there so decode them once again to get rid of them.
	//Due to bad webmasters there can be double-encoded entities in the title. Technically it is
	//their error but we can make some repairs on those pages.
	const char *title    = rawTitle;
	int32_t     titleLen = rawTitleLen;
	StackBuf<1024> doubleDecodedContent;
	possiblyDecodeHtmlEntitiesAgain(&title, &titleLen, &doubleDecodedContent, false);
	
	//get language and country if known, so tokenizer phase 2 can do its magic
	lang_t lang_id;
	const char *countryCode;
	getLanguageAndCountry(&lang_id,&countryCode);
	
	TokenizerResult tr;
	plain_tokenizer_phase_1(title,titleLen,&tr);
	plain_tokenizer_phase_2(lang_id, countryCode, &tr);
	calculate_tokens_hashes(&tr);
	sortTokenizerResult(&tr);
	
	Bits bits;
	if(!bits.set(&tr))
		return false;
	
	HashInfo hi;
	hi.m_tt        = tt;
	hi.m_hashGroup = HASHGROUP_TITLE;
	
	// hash with title: prefix
	hi.m_prefix    = "title";
	if(!hashWords3(&hi, &tr, NULL, &bits, NULL, NULL, NULL, m_wts, &m_wbuf))
		return false;
	// hash without title: prefix
	hi.m_prefix    = NULL;
	if(!hashWords3(&hi, &tr, NULL, &bits, NULL, NULL, NULL, m_wts, &m_wbuf))
		return false;
	
	return true;
}

// . we now do the title hashing here for newer titlerecs, version 80+, rather
//   than use the <index> block in the ruleset for titles.
// . this is not to be confused with hashing the title: terms which still
//   does have an <index> block in the ruleset.
bool XmlDoc::hashBody2 ( HashTableX *tt ) {

	// do not index ANY of the body if it is NOT a permalink and
	// "menu elimination" technology is enabled.
	//if ( ! *getIsPermalink() && m_eliminateMenus ) return true;

	setStatus ( "hashing body" );

	// record this
	m_bodyStartPos = m_dist;
	m_bodyStartPosValid = true;

	HashInfo hi;
	hi.m_tt         = tt;
	hi.m_desc       = "body";
	hi.m_hashGroup  = HASHGROUP_BODY;

	// use NULL for the prefix
	return hashWords (&hi );
}

bool XmlDoc::hashMetaKeywords ( HashTableX *tt ) {

	// do not index meta tags if "menu elimination" technology is enabled.
	//if ( m_eliminateMenus ) return true;

	setStatus ( "hashing meta keywords" );

	// hash the meta keywords tag
	//char buf [ 2048 + 2 ];
	//int32_t len=m_xml.getMetaContentPointer ( buf , 2048 , "keywords" , 8 );
	int32_t mklen;
	const char *mk = getMetaKeywords( &mklen );

	// update hash parms
	HashInfo hi;
	hi.m_tt         = tt;
	hi.m_desc       = "meta keywords";
	hi.m_hashGroup  = HASHGROUP_INMETATAG;

	// call XmlDoc::hashString
	return hashString4(mk, mklen, &hi);
}


void XmlDoc::lookupAndSetExplicitKeywords() {
	std::string kw;
	kw = ExplicitKeywords::lookupExplicitKeywords(m_firstUrl.getUrl());
	if(kw.empty())
		kw = ExplicitKeywords::lookupExplicitKeywords(m_currentUrl.getUrl());
	if(!kw.empty()) {
		log(LOG_DEBUG,"spider: found explicit keywords '%s' for %s", kw.c_str(),m_firstUrl.getUrl());
		m_explicitKeywordsBuf.set(kw.c_str());
		ptr_explicitKeywords = m_explicitKeywordsBuf.getBufStart();
		size_explicitKeywords = m_explicitKeywordsBuf.length();
	} else {
		m_explicitKeywordsBuf.purge();
		ptr_explicitKeywords = NULL;
		size_explicitKeywords = 0;
	}
}

bool XmlDoc::hashExplicitKeywords(HashTableX *tt) {
	if(m_version<128)
		return true;
	setStatus("hashing explicit keywords");
	
	if(size_explicitKeywords>0) {
		log(LOG_DEBUG,"spider: hashing explicit keywords '%.*s' for %s", size_explicitKeywords, ptr_explicitKeywords, m_firstUrl.getUrl());
		// update hash parms
		HashInfo hi;
		hi.m_tt         = tt;
		hi.m_desc       = "explicit keywords";
		hi.m_hashGroup  = HASHGROUP_EXPLICIT_KEYWORDS;
		return hashString4(ptr_explicitKeywords, size_explicitKeywords, &hi);
	} else
		return true; //nothing done - no error
}


// . hash the meta summary, description and keyword tags
// . we now do the title hashing here for newer titlerecs, version 80+, rather
//   than use the <index> block in the ruleset for titles.
bool XmlDoc::hashMetaSummary ( HashTableX *tt ) {

	// sanity check
	if ( m_hashedMetas ) { g_process.shutdownAbort(true); }

	// this has been called, note it
	m_hashedMetas = true;

	// do not index meta tags if "menu elimination" technology is enabled.
	//if ( m_eliminateMenus ) return true;

	setStatus ( "hashing meta summary" );

	StackBuf<1024> doubleDecodedContent;

	// hash the meta keywords tag
	//char buf [ 2048 + 2 ];
	//int32_t len = m_xml.getMetaContent ( buf , 2048 , "summary" , 7 );
	int32_t mslen;
	const char *ms = getMetaSummary ( &mslen );
	possiblyDecodeHtmlEntitiesAgain(&ms, &mslen, &doubleDecodedContent, true);

	// update hash parms
	HashInfo hi;
	hi.m_tt         = tt;
	hi.m_hashGroup  = HASHGROUP_INMETATAG;

	// udpate hashing parms
	hi.m_desc = "meta summary";
	// hash it
	if(!hashString4(ms,mslen,&hi))
		return false;


	//len = m_xml.getMetaContent ( buf , 2048 , "description" , 11 );
	int32_t mdlen;
	const char *md = getMetaDescription ( &mdlen );
	possiblyDecodeHtmlEntitiesAgain(&md, &mdlen, &doubleDecodedContent, true);

	// udpate hashing parms
	hi.m_desc = "meta desc";
	// . TODO: only hash if unique????? set a flag on ht then i guess
	if(!hashString4(md,mdlen, &hi))
		return false;

	return true;
}


bool XmlDoc::hashMetaGeoPlacename( HashTableX *tt ) {

	setStatus ( "hashing meta geo.placename" );

	int32_t mgplen;
	const char *mgp = getMetaGeoPlacename( &mgplen );

	// update hash parms
	HashInfo hi;
	hi.m_tt         = tt;
	hi.m_desc       = "meta geo.placename";
	hi.m_hashGroup  = HASHGROUP_INMETATAG;

	// call XmlDoc::hashString
	return hashString4(mgp, mgplen, &hi);
}




bool XmlDoc::hashLanguage ( HashTableX *tt ) {

	setStatus ( "hashing language" );

	int32_t langId = (int32_t)*getLangId();

	char s[32]; // numeric langid
	int32_t slen = sprintf(s, "%" PRId32, langId );

	// update hash parms
	HashInfo hi;
	hi.m_tt        = tt;
	hi.m_hashGroup = HASHGROUP_INTAG;
	hi.m_prefix    = "gblang";

	if ( ! hashString ( s, slen, &hi ) ) return false;

	return true;
}

bool XmlDoc::hashLanguageString ( HashTableX *tt ) {

	setStatus ( "hashing language string" );

	int32_t langId = (int32_t)*getLangId();

	// update hash parms
	HashInfo hi;
	hi.m_tt        = tt;
	hi.m_hashGroup = HASHGROUP_INTAG;
	hi.m_prefix    = "gblang";

	// try lang abbreviation
	char s[32];
	int32_t slen = sprintf(s , "%s ", getLanguageAbbr(langId) );
	// go back to broken way to try to fix parsing consistency bug
	if ( ! hashString ( s, slen, &hi ) ) return false;

	return true;
}

bool XmlDoc::hashCountry ( HashTableX *tt ) {

	setStatus ( "hashing country" );

	uint16_t *cid = getCountryId();
	if ( ! cid || cid == (uint16_t *)-1 ) return false;

	// update hash parms
	HashInfo hi;
	hi.m_tt        = tt;
	hi.m_hashGroup = HASHGROUP_INTAG;
	hi.m_prefix    = "gbcountry";

	for ( int32_t i = 0 ; i < 1 ; i++ ) {
		// convert it
		char buf[32];
		int32_t blen = sprintf(buf,"%s", g_countryCode.getAbbr(*cid) );
		// hash it
		if ( ! hashString ( buf, blen, &hi ) ) return false;
	}
	// all done
	return true;
}


bool XmlDoc::hashLemmas(HashTableX *table) {
	setStatus("hashing lemmas"); //Not llamas
	logTrace(g_conf.m_logTraceTokenIndexing,"lemma_words.size()=%zu", lemma_words.size());
	HashInfo hi; //storeTerm wants a HashInfo instance.

	if(m_dist > MAXWORDPOS) {
		log(LOG_INFO,"hashLemmas(): wordpos limit hit in document %.*s", m_firstUrl.getUrlLen(), m_firstUrl.getUrl());
		return true;
	}

	for(const auto &e : lemma_words) {
		uint64_t h = hash64Lower_utf8(e.data(),e.length());
		logTrace(g_conf.m_logTraceTokenIndexing,"Indexing lemma '%s', h=%ld, termid=%lld", e.c_str(), h, h&TERMID_MASK);
		key144_t k;
		Posdb::makeKey(&k,
			       h,
			       0LL,//docid
			       m_dist,
			       0,// densityRank , // 0-15
			       0, //diversityrank
			       0, //wordspamrank
			       0, // siterank
			       HASHGROUP_LEMMA,
			       m_langId, // we set to docLang final hash loop
			       0, // multiplier
			       false, // syn?
			       false, // delkey?
			       false); //shardByTermId
		table->addTerm144(&k);

		if(m_wts) {
			// add to wts for PageParser.cpp display
			if(!storeTerm(e.data(),e.length(),
				      h, &hi,
				      0, //word index. We could keep track of the first word that generated this base form. But we don't.
				      m_dist, // wordPos
				      0,// densityRank , // 0-15
				      0, //diversityrank
				      0, //wordspamrank
				      HASHGROUP_LEMMA,
				      &m_wbuf,
				      m_wts,
				      SOURCE_NONE, // synsrc
				      m_langId,
				      k))
				return false;
		}
	}
	return true;
}


void XmlDoc::sortTokenizerResult(TokenizerResult *tr) {
	std::sort(tr->tokens.begin(), tr->tokens.end(), [](const TokenRange&tr0, const TokenRange &tr1) {
		return tr0.start_pos < tr1.start_pos ||
		       (tr0.start_pos == tr1.start_pos && tr0.end_pos<tr1.end_pos);
	});
}

void XmlDoc::getLanguageAndCountry(lang_t *lang, const char **country_code) {
	//get language and country if known, so tokenizer phase 2 can do its magic
	uint8_t *tmpLangId = getLangId();
	if(tmpLangId!=NULL && tmpLangId!=(uint8_t*)-1)
		*lang = (lang_t)*tmpLangId;
	else
		*lang = langUnknown;
	
	uint16_t *countryId = getCountryId();
	if(countryId!=NULL && countryId!=(uint16_t*)-1)
		*country_code = g_countryCode.getAbbr(*countryId);
	else
		*country_code = NULL;
}

bool XmlDoc::hashSingleTerm( const char *s, int32_t slen, HashInfo *hi ) {
	// empty?
	if ( slen <= 0 ) return true;
	if ( ! m_versionValid    ) { g_process.shutdownAbort(true); }
	if ( hi->m_useCountTable && ! m_countTableValid){g_process.shutdownAbort(true); }

	// a single blob hash
        int64_t termId = hash64 ( s , slen );
	// combine with prefix
	int64_t final = termId;
	// combine with a non-NULL prefix
	int64_t prefixHash = 0LL;
	if ( hi->m_prefix ) {
		prefixHash = hash64b ( hi->m_prefix );
		final = hash64 ( termId , prefixHash );
	}
	// call the other guy now
	//return hashSingleTerm ( final , hi );


	// shortcut
	HashTableX *dt = hi->m_tt;
	// sanity check
	if ( dt->getKeySize() != sizeof(key144_t) ) { g_process.shutdownAbort(true); }
	// make the key like we do in hashWords()


	key144_t k;
	Posdb::makeKey ( &k ,
			  final,
			  0LL, // docid
			  0, // dist
			  MAXDENSITYRANK, // density rank
			  MAXDIVERSITYRANK, // diversity rank
			  MAXWORDSPAMRANK, // wordspamrank
			  0, // siterank
			  hi->m_hashGroup,
			  // we set to docLang in final hash loop
			  langUnknown,// langid
			  0, // multiplier
			  0, // syn?
			  false , // delkey?
			  hi->m_shardByTermId );

	// . otherwise, add a new slot
	// . key should NEVER collide since we are always
	//   incrementing the distance cursor, m_dist
	if ( ! dt->addTerm144 ( &k ) ) return false;

	// add to wts for PageParser.cpp display
	if ( m_wts && ! storeTerm ( s,slen,final,hi,
				    0, // wordnum
				    0, // wordPos,
				    MAXDENSITYRANK,
				    MAXDIVERSITYRANK,
				    MAXWORDSPAMRANK,
				    hi->m_hashGroup,
				    //false,
				    &m_wbuf,
				    m_wts,
				    SOURCE_NONE, // synsrc
				    langUnknown,
				    k) )
		return false;

	return true;
}

bool XmlDoc::hashString( const char *s, int32_t slen, HashInfo *hi ) {
	if ( ! m_versionValid        ) { g_process.shutdownAbort(true); }

	if ( hi->m_useCountTable && ! m_countTableValid){g_process.shutdownAbort(true); }

	if ( ! m_siteNumInlinksValid ) { g_process.shutdownAbort(true); }

	return   hashString3( s                ,
			      slen             ,
			      hi               ,
			      m_wts            ,
			      &m_wbuf          );
}

bool XmlDoc::hashString(size_t begin_token, size_t end_token, HashInfo *hi) {
	if(!m_versionValid)
		gbshutdownLogicError();
	return hashString3(begin_token, end_token, hi,
			   m_wts,
			   &m_wbuf);
}


bool XmlDoc::hashString3( const char       *s              ,
		  int32_t        slen           ,
		  HashInfo   *hi             ,
		  HashTableX *wts            ,
		  SafeBuf    *wbuf) {
	TokenizerResult tr;
	Bits    bits;

	plain_tokenizer_phase_1(s,slen,&tr);
	calculate_tokens_hashes(&tr);
	if ( !bits.set(&tr))
		return false;

	// use primary langid of doc
	if ( ! m_langIdValid ) { g_process.shutdownAbort(true); }

	return hashWords3( hi, &tr, NULL, &bits, NULL, NULL, NULL, wts, wbuf );
}

bool XmlDoc::hashString3(size_t begin_token, size_t end_token, HashInfo *hi,
			 HashTableX *wts, SafeBuf *wbuf)
{
	Bits    bits;

	if ( !bits.set(&m_tokenizerResult))
		return false;

	return hashWords3( hi, &m_tokenizerResult, begin_token, end_token, NULL, &bits, NULL, NULL, NULL, wts, wbuf );
}

bool XmlDoc::hashString4(const char *s, int32_t slen, HashInfo *hi) {
	TokenizerResult tr;
	Bits    bits;
	lang_t lang_id;
	const char *countryCode;
	
	getLanguageAndCountry(&lang_id,&countryCode);
	plain_tokenizer_phase_1(s,slen,&tr);
	plain_tokenizer_phase_2(lang_id,countryCode,&tr);
	calculate_tokens_hashes(&tr);
	sortTokenizerResult(&tr);
	if(!bits.set(&tr))
		return false;

	return hashWords3( hi, &tr, NULL, &bits, NULL, NULL, NULL, m_wts, &m_wbuf );
}


bool XmlDoc::hashWords ( HashInfo   *hi ) {
	// sanity checks
	if ( ! m_tokenizerResultValid   ) { g_process.shutdownAbort(true); }
	if ( ! m_tokenizerResultValid2  ) { g_process.shutdownAbort(true); }
	//if ( hi->m_useCountTable &&!m_countTableValid){g_process.shutdownAbort(true); }
	if ( ! m_bitsValid ) { g_process.shutdownAbort(true); }
	if ( ! m_sectionsValid) { g_process.shutdownAbort(true); }
	//if ( ! m_synonymsValid) { g_process.shutdownAbort(true); }
	if ( ! m_fragBufValid ) { g_process.shutdownAbort(true); }
	if ( ! m_wordSpamBufValid ) { g_process.shutdownAbort(true); }
	if ( m_wts && ! m_langVectorValid  ) { g_process.shutdownAbort(true); }
	if ( ! m_langIdValid ) { g_process.shutdownAbort(true); }
	// . is the word repeated in a pattern?
	// . this should only be used for document body, for meta tags,
	//   inlink text, etc. we should make sure words are unique
	char *wordSpamVec = getWordSpamVec();
	char *fragVec = m_fragBuf.getBufStart();
	char *langVec = m_langVec.getBufStart();

	return hashWords3(hi, &m_tokenizerResult, &m_sections, &m_bits, fragVec, wordSpamVec, langVec, m_wts, &m_wbuf);
}

// . this now uses posdb exclusively
bool XmlDoc::hashWords3(HashInfo *hi, const TokenizerResult *tr,
			Sections *sections, const Bits *bits,
			const char *fragVec, const char *wordSpamVec, const char *langVec,
			HashTableX *wts, SafeBuf *wbuf)
{
	return hashWords3(hi,tr, 0,tr->size(), sections, bits, fragVec, wordSpamVec, langVec, wts, wbuf);
}

bool XmlDoc::hashWords3(HashInfo *hi, const TokenizerResult *tr, size_t begin_token, size_t end_token,
			Sections *sections, const Bits *bits,
			const char *fragVec, const char *wordSpamVec, const char *langVec,
			HashTableX *wts, SafeBuf *wbuf)
{
	// for getSpiderStatusDocMetaList() we don't use sections it'll mess us up
	if ( ! hi->m_useSections ) sections = NULL;

	HashTableX *dt = hi->m_tt;
	std::unordered_set<std::string> candidate_lemma_words;

	// . sanity checks
	// . posdb just uses the full keys with docid
	if ( dt->getKeySize() != 18 ) { g_process.shutdownAbort(true); }
	if ( dt->getDataSize() != 4  ) { g_process.shutdownAbort(true); }

	// if provided...
	if ( wts ) {
		if ( wts->getKeySize() != 12               ) { g_process.shutdownAbort(true); }
		if ( wts->getDataSize() != sizeof(TermDebugInfo)){g_process.shutdownAbort(true); }
		if ( ! wts->isAllowDups() ) { g_process.shutdownAbort(true); }
	}

	// ensure caller set the hashGroup
	if ( hi->m_hashGroup < 0 ) { g_process.shutdownAbort(true); }

	// hash in the prefix
	uint64_t prefixHash = 0LL;
	int32_t plen = 0;
	if ( hi->m_prefix ) plen = strlen ( hi->m_prefix );
	if ( hi->m_prefix && plen ) {
		// we gotta make this case insensitive, and skip spaces
		// because if it is 'focal length' we can't search
		// 'focal length:10' because that comes across as TWO terms.
		prefixHash = hash64Lower_utf8_nospaces ( hi->m_prefix , plen );
		// . sanity test, make sure it is in supported list
		// . hashing diffbot json output of course fails this so
		//   skip in that case if diffbot
	}

	bool hashIffUnique = false;
	if ( hi->m_hashGroup == HASHGROUP_INMETATAG  ) hashIffUnique = true;
	if ( hi->m_hashGroup == HASHGROUP_INTAG      ) hashIffUnique = true;
	HashTableX ut; ut.set ( 8,0,0,NULL,0,false,"uqtbl");

	//The diversity rank was effectively disabled (minweight=maxweigt) and the algortihm was either suspect or severely limited by phrases being only 2 words (bigrams).
	//Currently disabled until we can investigate if it is worth fixing, worth implementing in another way, or simply dropped completely.
	//
	//Diversityrank is currently hardcoded to be 10 for individual words, and maxdiversityrank for bigrams
	SafeBuf dwbuf;
	if(!dwbuf.reserve(tr->size()*sizeof(char)))
		return false;
	memset(dwbuf.getBufStart(), MAXDIVERSITYRANK, tr->size());
#if 0
	///////
	//
	// diversity rank vector.
	//
	///////
	// the final diversity which is a multiplier
	// is converted into a rank from 0-15 i guess.
	// so 'mexico' in "new mexico" should receive a low word score but high
	// phrase score. thus, a search for 'mexico' should not bring up
	// the page for university of new mexico!
	SafeBuf dwbuf;
	if ( !getDiversityVec( tr, phrases, countTable, &dwbuf ) ) {
		return false;
	}
#endif
	char *wdv = dwbuf.getBufStart();

	size_t nw = tr->size();

	/////
	//
	// calculate density ranks
	//
	/////
	//
	// this now varies depending on the length of the sentence/header etc.
	// so if the hasgroup is not title, link text or meta tag, we have to
	// use a safebuf.
	SafeBuf densBuf;
	// returns false and sets g_errno on error
	if ( ! getDensityRanks(tr,
			       hi->m_hashGroup,
			       &densBuf,
			       sections))
		return false;
	// a handy ptr
	char *densvec = (char *)densBuf.getBufStart();

	////////////
	//
	// get word positions
	//
	///////////
	Section **sp = NULL;
	if ( sections ) sp = sections->m_sectionPtrs;

	SafeBuf wpos;
	if ( ! getWordPosVec ( tr, sections, m_dist, fragVec, &wpos) )
		return false;

	// a handy ptr
	int32_t *wposvec = (int32_t *)wpos.getBufStart();

	if(end_token>begin_token && wposvec[end_token-1]>MAXWORDPOS) {
		log(LOG_INFO,"hashWords3(): wordpos limit will be hit in document %.*s", m_firstUrl.getUrlLen(), m_firstUrl.getUrl());
	}

	bool seen_slash = false;
	for(unsigned i = begin_token; i < end_token; i++) {
		const auto &token = (*tr)[i];
		logTrace(g_conf.m_logTraceTokenIndexing,"Looking at token #%u: '%.*s', hash=%ld, nodeid=%u", i, (int)token.token_len, token.token_start, token.token_hash, token.nodeid);
		if(token.token_len==1 && token.token_start[0]=='/')
			seen_slash = true;
		
		if ( ! token.is_alfanum ) continue;
		// ignore if in repeated fragment
		if ( fragVec && i<MAXFRAGWORDS && fragVec[i] == 0 ) continue;
		// ignore if in style section
		if ( sp && (sp[i]->m_flags & NOINDEXFLAGS) ) continue;

		// do not breach wordpos bits
		if ( wposvec[i] > MAXWORDPOS ) break;

		// BR: 20160114 if digit, do not hash it if disabled
		if( is_digit( token.token_start[0] ) && !hi->m_hashNumbers ) {
			continue;
		}

		// . hash the startHash with the wordId for this word
		// . we must mask it before adding it to the table because
		//   this table is also used to hash IndexLists into that come
		//   from LinkInfo classes (incoming link text). And when
		//   those IndexLists are hashed they used masked termIds.
		//   So we should too...
		uint64_t h ;
		if ( plen > 0 ) h = hash64 ( token.token_hash, prefixHash );
		else            h = token.token_hash;

		int32_t hashGroup = hi->m_hashGroup;

		Section *sx = NULL;
		if ( sp ) {
			sx = sp[i];
			// . this is taken care of in hashTitle()
			// . it is slightly different if the title is
			//   multiple sentences because when hashing the
			//   body the density rank is per sentence, but in
			//   hashTitle we count all the words in the title
			//   towards the density rank even if they are
			//   in different sentences
			if ( sx->m_flags & SEC_IN_TITLE  ) {
				continue;
			}
			if ( sx->m_flags & SEC_IN_HEADER ) {
				hashGroup = HASHGROUP_HEADING;
			}
			if ( sx->m_flags & ( SEC_MENU | SEC_MENU_SENTENCE | SEC_MENU_HEADER ) ) {
				hashGroup = HASHGROUP_INMENU;
			}
		}

		// this is for link text and meta tags mostly
		if ( hashIffUnique ) {
			// skip if already did it
			if ( ut.isInTable ( &h ) ) continue;
			if ( ! ut.addKey ( &h ) ) return false;
		}

		char ws = 15;
		if ( wordSpamVec ) ws = wordSpamVec[i];

		// HACK:
		// if this is inlink text, use the wordspamrank to hold the
		// inlinker's site rank!
		if ( hashGroup == HASHGROUP_INLINKTEXT )
			ws = hi->m_linkerSiteRank;

		// default to the document's primary language if it is not
		// clear what language this word belongs to.
		// if the word is only in german it should be german,
		// otherwise it will be the document's primary language.
		//note: the above comment is wrong. The lanauge is overwritten by addTable144(). It is unclear if this is a good thing
		char langId = langUnknown;
		if ( m_wts && langVec ) langId = langVec[i];

		char wd;
		if ( hi->m_useCountTable ) {
			wd = wdv[i];
		} else {
			wd = MAXDIVERSITYRANK;
		}

		bool skipword = false;
		if(hi->m_filterUrlIndexableWords) {
			if(!seen_slash) {
				//Scheme/host/domain part of URL
				//the http/https prefix is not indexed at all
				if((token.token_len==4 && memcmp(token.token_start,"http",4)==0) ||
				   (token.token_len==5 && memcmp(token.token_start,"https",5)==0))
				{
					// Never include as single word or in bigrams
					continue; //skip to next word
				}
				//the terms .com .co .dk etc have lots of hits and give very little value for indexing. We only index the bigrams.
				if(isTLD(token.token_start, token.token_len)) {
					skipword = true; //skip word by index bigram
				}
			} else {
				//Path parth for URL
				//potentially filter out "html" "aspx" index" "cgi" etc.
			}
		}

		if(!skipword) {
			logTrace(g_conf.m_logTraceTokenIndexing,"Indexing '%.*s', h=%ld, termid=%lld", (int)token.token_len, token.token_start, h, h&TERMID_MASK);
			key144_t k;

			Posdb::makeKey(&k,
				       h,
				       0LL,//docid
				       wposvec[i], // dist,
				       densvec[i],// densityRank , // 0-15
				       wd, // diversityRank 0-15
				       ws, // wordSpamRank  0-15
				       0, // siterank
				       hashGroup,
				       // we set to docLang final hash loop
				       langUnknown, // langid
				       0, // multiplier
				       false, // syn?
				       false, // delkey?
				       hi->m_shardByTermId);

			// key should NEVER collide since we are always incrementing
			// the distance cursor, m_dist
			dt->addTerm144(&k);

			// add to wts for PageParser.cpp display
			if(wts) {
				if(!storeTerm(token.token_start,token.token_len,h,hi,i,
					      wposvec[i], // wordPos
					      densvec[i],// densityRank , // 0-15
					      wd,//v[i],
					      ws,
					      hashGroup,
					      wbuf,
					      wts,
					      SOURCE_NONE, // synsrc
					      langId,
					      k))
					return false;
			}
			if(token.is_alfanum)
				candidate_lemma_words.emplace(token.token_start,token.token_len);
		} else {
			logTrace(g_conf.m_logTraceTokenIndexing,"not indexing '%.*s', h=%ld", (int)token.token_len, token.token_start, h);
		}


		////////
		//
		// two-word phrase
		//
		////////

		//Find the first next alfanum token that starts at or after token.end_pos
		//Also detect if we see a dont-pair-across token while scanning
		unsigned j;
		bool generate_bigram = true;
		for(j=i+1; j<end_token; j++) {
			const auto &t2 = (*tr)[j];
			if(t2.is_alfanum && t2.start_pos>=token.end_pos)
				break;
			if(!bits->canBeInPhrase(j) && !bits->canPairAcross(j)) {
				generate_bigram = false;
				break;
			}
		}
		if(j>=end_token)
			generate_bigram = false;
		
		if(generate_bigram) {
			unsigned first_match_start_pos = (*tr)[j].start_pos;
			for( ; j<end_token && (*tr)[j].start_pos == first_match_start_pos; j++) {
				const auto &token2 = (*tr)[j];
				if(!token2.is_alfanum)
					continue; //ampersand-rewrites in tokenizer2.cpp can result in non-alfanum tokens that must be ignored and skipped
				int32_t pos = token.token_len;
				int64_t npid = hash64Lower_utf8_cont(token2.token_start, token2.token_len, token.token_hash, &pos);
				uint64_t  ph2;

				logTrace(g_conf.m_logTraceTokenIndexing,"Indexing two-word phrase '%.*s'+'%.*s' with h=%ld, termid=%lld", (int)token.token_len, token.token_start, (int)token2.token_len, token2.token_start, npid, npid&TERMID_MASK);
				// hash with prefix
				if ( plen > 0 ) ph2 = hash64 ( npid , prefixHash );
				else            ph2 = npid;
				key144_t k;
				Posdb::makeKey ( &k ,
						ph2 ,
						0LL,//docid
						wposvec[i],//dist,
						densvec[i],// densityRank , // 0-15
						MAXDIVERSITYRANK, //phrase
						ws, // wordSpamRank ,
						0,//siterank
						hashGroup,
						// we set to docLang final hash loop
						langUnknown, // langid
						0 , // multiplier
						false, // syn?
						false , // delkey?
						hi->m_shardByTermId );

				// key should NEVER collide since we are always
				// incrementing the distance cursor, m_dist
				dt->addTerm144 ( &k );

				// add to wts for PageParser.cpp display
				if(wts) {
					// get phrase as a string
					size_t plen;
					char phraseBuffer[256];
					//TODO: Collect the intermediate tokens too. It is complicated because the two tokens generating the bigram can be either primary or secondary tokens from the tonizer, and the non-alfanum tokens between too.
					//simplification: just grab the chars from token+token2
					if(token.token_len<=sizeof(phraseBuffer)) {
						memcpy(phraseBuffer, token.token_start, token.token_len);
						plen = token.token_len;
					} else {
						memcpy(phraseBuffer, token.token_start, sizeof(phraseBuffer));
						plen = sizeof(phraseBuffer);
					}
					if(token2.token_len<=sizeof(phraseBuffer)-plen) {
						memcpy(phraseBuffer+plen, token2.token_start, token2.token_len);
						plen += token2.token_len;
					} else {
						memcpy(phraseBuffer+plen, token2.token_start, sizeof(phraseBuffer)-plen);
						plen = sizeof(phraseBuffer);
					}
					// store it
					if(!storeTerm(phraseBuffer,plen,ph2,hi,i,
						wposvec[i], // wordPos
						densvec[i],// densityRank , // 0-15
						MAXDIVERSITYRANK,//phrase
						ws,
						hashGroup,
						//true,
						wbuf,
						wts,
						SOURCE_BIGRAM, // synsrc
						langId,
						k))
						return false;
				}
			}
		} else {
			logTrace(g_conf.m_logTraceTokenIndexing,"NOT indexing two-word phrase(s)");
		}
	}

	// between calls? i.e. hashTitle() and hashBody()
	if ( nw > 0 ) m_dist = wposvec[nw-1] + 100;

	if(m_langId==langDanish) {
		//we only have a lexicon for Danish so far for this test
		logTrace(g_conf.m_logTraceTokenIndexing,"candidate_lemma_words.size()=%zu", candidate_lemma_words.size());
		for(auto e : candidate_lemma_words) {
			//find the word in the lexicon. find the lemma. If the word is unknown or already in its base form then don't generate a lemma entry
			logTrace(g_conf.m_logTraceTokenIndexing,"candidate  word for lemma: %s", e.c_str());
			auto le = lemma_lexicon.lookup(e);
			if(!le) {
				//Not found as-is in lexicon. Try lowercase in case it is a capitalized word
				char lowercase_word[128];
				if(e.size()<sizeof(lowercase_word)) {
					size_t sz = to_lower_utf8(lowercase_word,lowercase_word+sizeof(lowercase_word), e.data(), e.data()+e.size());
					lowercase_word[sz] = '\0';
					if(sz!=e.size() || memcmp(e.data(),lowercase_word,e.size())!=0) {
						e = lowercase_word;
						le = lemma_lexicon.lookup(e);
					}
				}
			}
			if(!le)
				continue; //unknown word
			logTrace(g_conf.m_logTraceTokenIndexing,"lexicalentry found for for lemma: %s", e.c_str());
			
			auto wf = le->find_base_wordform();
			if(!wf)
				continue; //no base form
			if(wf->written_form_length==e.size() && memcmp(wf->written_form,e.data(),e.size())==0)
				continue; //already in base form
			logTrace(g_conf.m_logTraceTokenIndexing,"baseform is different than source: '%.*s'", (int)wf->written_form_length, wf->written_form);
			lemma_words.emplace(wf->written_form,wf->written_form_length);
		}
	}
	
	return true;
}
