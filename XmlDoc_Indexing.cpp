//-*- coding: utf-8 -*-

#include "gb-include.h"

#include "XmlDoc.h"
#include "CountryCode.h" // g_countryCode
#include "Speller.h"
#include "Synonyms.h"
#ifdef _VALGRIND_
#include <valgrind/memcheck.h>
#endif


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

		// BR 20160108: Now default to false since we will only use it for
		// very specific cases like spiderdate, which is for debugging only.
		// If true, creates 4 posdb entries for numbers in posdb, e.g.
		// gbsortbyint:gbisadultint32, gbrevsortbyint:gbisadultint32
		// gbsortby:gbisadultfloat32, gbrevsortby:gbisadultfloat32
		m_createSortByForNumbers= false;
		m_hashNumbers			= true;
		m_hashCommonWebWords	= true;
	};
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
	bool			m_createSortByForNumbers;
	bool			m_hashNumbers;
	bool			m_hashCommonWebWords;
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
		 POSDBKEY key ) {

	// store prefix
	int32_t poff = wbuf->length();
	// shortcut
	const char *p = hi->m_prefix;
	// add the prefix too!
	if ( p  && ! wbuf->safeMemcpy(p,gbstrlen(p)+1)) return false;
	// none?
	if ( ! p ) poff = -1;


	// store description
	int32_t doff = wbuf->length();
	// shortcut
	const char *d = hi->m_desc;
	// add the desc too!
	if ( d && ! wbuf->safeMemcpy(d,gbstrlen(d)+1) ) return false;
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
	ti.m_langBitVec64 = g_speller.getLangBits64(&termId);

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
// . these terms are stored in indexdb/datedb, but all terms with the same
//   termId reside in one and only one group. whereas normally the records
//   are split based on docid and every group gets 1/nth of the termlist.
// . we do this "no splitting" so that only one disk seek is required, and
//   we know the termlist is small, or the termlist is being used for spidering
//   or parsing purposes and is usually not sent across the network.
bool XmlDoc::hashNoSplit ( HashTableX *tt ) {
	// this should be ready to go and not block!
	int64_t *pch64 = getExactContentHash64();
	if ( ! pch64 || pch64 == (void *)-1 ) { char *xx=NULL;*xx=0; }

	// shortcut
	Url *fu = getFirstUrl();

	// constructor should set to defaults automatically
	HashInfo hi;
	hi.m_hashGroup = HASHGROUP_INTAG;
	hi.m_tt        = tt;
	// usually we shard by docid, but these are terms we shard by termid!
	hi.m_shardByTermId   = true;


	// for exact content deduping
	setStatus ( "hashing gbcontenthash (deduping) no-split keys" );
	char cbuf[64];
	int32_t clen = sprintf(cbuf,"%"UINT64"",*pch64);
	hi.m_prefix    = "gbcontenthash";
	if ( ! hashString ( cbuf,clen,&hi ) ) return false;

	////
	//
	// let's stop here for now, until other stuff is actually used again
	//
	////

	// let's bring back image thumbnail support for the widget project
	//return true;



	char *host = fu->getHost    ();
	//int32_t  hlen = fu->getHostLen ();

	/*
	setStatus ( "hashing no-split qdom keys" );

	char *dom  = fu->getDomain   ();
	int32_t  dlen = fu->getDomainLen();

	// desc is NULL, prefix will be used as desc
	hi.m_prefix    = "qdom";
	if ( ! hashString ( dom,dlen,&hi ) ) return false;


	setStatus ( "hashing no-split qhost keys" );

	// desc is NULL, prefix will be used as desc
	hi.m_prefix = "qhost";
	if ( ! hashString ( host,hlen,&hi ) ) return false;
	*/


	// now hash the site


	setStatus ( "hashing no-split SiteGetter terms");

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
	if ( ! m_linksValid ) { char *xx=NULL; *xx=0; }
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

	//Dates *dp = getDates ();
	// hash the clocks into indexdb
	//if ( ! dp->hash ( m_docId , tt , this ) ) return false;

	// . hash special site/hopcount thing for permalinks
	// . used by Images.cpp for doing thumbnails
	// . this returns false and sets g_errno on error
	// . let's try thumbnails for all...
	//if ( ! *getIsPermalink() ) return true;

/*
	BR 20160117: No longer has image URLs
	setStatus ( "hashing no-split gbimage keys" );

	hi.m_prefix    = "gbimage";
	// hash gbimage: for permalinks only for Images.cpp
	for ( int32_t i = 0 ; i < m_images.m_numImages ; i++ ) {
		// get the node number
		//int32_t nn = m_images.m_imageNodes[i];
		// get the url of the image
		//XmlNode *xn = m_xml.getNodePtr(nn);
		int32_t  srcLen;
		char *src = m_images.getImageUrl(i,&srcLen);
		// set it to the full url
		Url iu;
		// use "pageUrl" as the baseUrl
		Url *cu = getCurrentUrl();
		// we can addwww to normalize since this is for deduping kinda
		iu.set ( cu , src , srcLen , true );  // addWWW? yes...
		char *u    = iu.getUrl   ();
		int32_t  ulen = iu.getUrlLen();
		// hash each one
		//if ( ! hashString ( u,ulen,&hi ) ) return false;
		// hash a single entity
		if ( ! hashSingleTerm ( u,ulen,&hi) ) return false;
		//log("test: %s",u);
	}
*/
	return true;
}

// . returns -1 if blocked, returns NULL and sets g_errno on error
// . "sr" is the tagdb Record
// . "ws" store the terms for PageParser.cpp display
char *XmlDoc::hashAll ( HashTableX *table ) {

	if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: BEGIN", __FILE__,__func__, __LINE__);
		
	setStatus ( "hashing document" );

	if ( m_allHashed ) return (char *)1;

	// sanity checks
	if ( table->m_ks != 18 ) { char *xx=NULL;*xx=0; }
	if ( table->m_ds != 4  ) { char *xx=NULL;*xx=0; }

	if ( m_wts && m_wts->m_ks != 12  ) { char *xx=NULL;*xx=0; }
	// ptr to term = 4 + score = 4 + ptr to sec = 4
	if ( m_wts && m_wts->m_ds!=sizeof(TermDebugInfo)){char *xx=NULL;*xx=0;}

	uint8_t *ct = getContentType();
	if ( ! ct )
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, getContentType failed", __FILE__,__func__, __LINE__);
		return NULL;
	}
	
	// BR 20160127: Never index JSON and XML content
	if ( *ct == CT_JSON || *ct == CT_XML )
	{
		// For XML (JSON should not get here as it should be filtered out during spidering)
		// store the URL as the only thing in posdb so we are able to find it, and
		// eventually ban it.
		if ( !hashUrl( table, true ) )  // urlOnly (skip IP and term generation)
		{
			if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashUrl failed", __FILE__,__func__, __LINE__);
			return NULL;
		}
		m_allHashed = true;
		return (char *)1;
	}



	unsigned char *hc = (unsigned char *)getHopCount();
	if ( ! hc || hc == (void *)-1 ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, getHopCount returned -1", __FILE__,__func__, __LINE__);
		return (char *)hc;
	}

	// need this for hashing
	HashTableX *cnt = getCountTable();
	if ( ! cnt ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, getCountTable failed", __FILE__,__func__, __LINE__);
		return (char *)cnt;
	}
	if ( cnt == (void *)-1 ) { char *xx=NULL;*xx=0; }

	// and this
	Links *links = getLinks();
	if ( ! links ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, getLinks failed", __FILE__,__func__, __LINE__);
		return (char *)links;
	}
	if ( links == (Links *)-1 ) { char *xx=NULL;*xx=0; }

	char *wordSpamVec = getWordSpamVec();
	if (!wordSpamVec) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, getWordSpamVec failed", __FILE__,__func__, __LINE__);
		return (char *)wordSpamVec;
	}
	if (wordSpamVec==(void *)-1) {char *xx=NULL;*xx=0;}

	char *fragVec = getFragVec();//m_fragBuf.getBufStart();
	if ( ! fragVec ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, getFragVec failed", __FILE__,__func__, __LINE__);
		return (char *)fragVec;
	}
	if ( fragVec == (void *)-1 ) { char *xx=NULL;*xx=0; }

	// why do we need this?
	if ( m_wts ) {
		uint8_t *lv = getLangVector();
		if ( ! lv ) 
		{
			if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, getLangVector failed", __FILE__,__func__, __LINE__);
			return (char *)lv;
		}
		if ( lv == (void *)-1 ) { char *xx=NULL;*xx=0; }
	}

	CollectionRec *cr = getCollRec();
	if ( ! cr ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, getCollRec failed", __FILE__,__func__, __LINE__);
		return NULL;
	}


	// do not repeat this if the cachedb storage call blocks
	m_allHashed = true;

	// reset distance cursor
	m_dist = 0;


	if ( ! hashContentType   ( table ) ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashContentType failed", __FILE__,__func__, __LINE__);
		return NULL;
	}
	
	if ( ! hashUrl           ( table, false ) ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashUrl failed", __FILE__,__func__, __LINE__);
		return NULL;
	}
	
	if ( ! hashLanguage      ( table ) ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashLanguage failed", __FILE__,__func__, __LINE__);
		return NULL;
	}
	
	if ( ! hashCountry       ( table ) ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashCountry failed", __FILE__,__func__, __LINE__);
		return NULL;
	}

// BR 20160106 removed:	if ( ! hashAds           ( table ) ) return NULL;
// BR 20160106 removed:	if ( ! hashSubmitUrls    ( table ) ) return NULL;
	if ( ! hashIsAdult       ( table ) ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashIsAdult failed", __FILE__,__func__, __LINE__);
		return NULL;
	}

	// has gbhasthumbnail:1 or 0
// BR 20160106 removed:	if ( ! hashImageStuff    ( table ) ) return NULL;

	// now hash the terms sharded by termid and not docid here since they
	// just set a special bit in posdb key so Rebalance.cpp can work.
	// this will hash the content checksum which we need for deduping
	// which we use for diffbot custom crawls as well.
	if ( ! hashNoSplit ( table ) ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashNoSplit failed", __FILE__,__func__, __LINE__);
		return NULL;
	}

	// MDW: i think we just inject empty html with a diffbotreply into
	// global index now, so don't need this... 9/28/2014

	// stop indexing xml docs
	bool indexDoc = true;
	if ( cr->m_isCustomCrawl ) indexDoc = false;
	if ( ! cr->m_indexBody   ) indexDoc = false;


	// global index unless this is a json object in which case it is
	// hashed above in the call to hashJSON(). this will decrease disk
	// usage by about half, posdb* files are pretty big.
	if ( ! indexDoc ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, !indexDoc", __FILE__,__func__, __LINE__);
		return (char *)1;
	}

	if ( *ct == CT_JSON || *ct == CT_XML ) {
		goto skip;
	}

	// hash the body of the doc first so m_dist is 0 to match
	// the rainbow display of sections
	if ( ! hashBody2 (table ) ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashBody2 failed", __FILE__,__func__, __LINE__);
		return NULL;
	}

	// hash the title now too so neighborhood singles have more
	// to match. plus, we only hash these title terms iff they
	// are not already in the hash table, so as to avoid hashing
	// repeated title terms because we do not do spam detection
	// on them. thus, we need to hash these first before anything
	// else. give them triple the body score
	if ( ! hashTitle ( table )) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashTitle failed", __FILE__,__func__, __LINE__);
		return NULL;
	}

	// . hash the keywords tag, limited to first 2k of them so far
	// . hash above the neighborhoods so the neighborhoods only index
	//   what is already in the hash table
	if ( ! hashMetaKeywords(table ) ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashMetaKeywords failed", __FILE__,__func__, __LINE__);
		return NULL;
	}

	// then hash the incoming link text, NO ANOMALIES, because
	// we index the single words in the neighborhoods next, and
	// we had songfacts.com coming up for the 'street light facts'
	// query because it had a bunch of anomalous inlink text.
	if ( ! hashIncomingLinkText(table,false,true)) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashIncomingLinkText failed", __FILE__,__func__, __LINE__);
		return NULL;
	}

	// then the meta summary and description tags with half the score of
	// the body, and only hash a term if was not already hashed above
	// somewhere.
	if ( ! hashMetaSummary(table) ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashMetaSummary failed", __FILE__,__func__, __LINE__);
		return NULL;
	}


	// BR 20160220
	// Store value of meta tag "geo.placename" to help aid searches for
	// location specific sites, e.g. 'Restaurant in London'
	if ( ! hashMetaGeoPlacename(table) ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashMetaGeoPlacename failed", __FILE__,__func__, __LINE__);
		return NULL;
	}



 skip:

	// this will only increment the scores of terms already in the table
	// because we neighborhoods are not techincally in the document
	// necessarily and we do not want to ruin our precision
	if ( ! hashNeighborhoods ( table ) ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashNeighborhoods failed", __FILE__,__func__, __LINE__);
		return NULL;
	}

	if ( ! hashLinks         ( table ) ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashLinks failed", __FILE__,__func__, __LINE__);
		return NULL;
	}
	
	if ( ! hashDateNumbers   ( table ) ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashDateNumbers failed", __FILE__,__func__, __LINE__);
		return NULL;
	}
	
	if ( ! hashMetaTags      ( table ) ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashMetaTags failed", __FILE__,__func__, __LINE__);
		return NULL;
	}

	if ( ! hashPermalink     ( table ) )
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashPermaLink failed", __FILE__,__func__, __LINE__);
		return NULL;
	}

	// hash gblang:de last for parsing consistency
	if ( ! hashLanguageString ( table ) ) 
	{
		if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, hashLanguageString failed", __FILE__,__func__, __LINE__);
		return NULL;
	}

	// . hash gbkeyword:gbmininlinks where the score is the inlink count
	// . the inlink count can go from 1 to 255
	// . an ip neighborhood can vote no more than once
	// . this is in LinkInfo::hash
	//if ( ! hashMinInlinks ( table , linkInfo ) ) return NULL;


	// return true if we don't need to print parser info
	//if ( ! m_pbuf ) return true;
	// print out the table into g_bufPtr now if we need to
	//table->print ( );
	if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: END, OK", __FILE__,__func__, __LINE__);
	return (char *)1;
}


bool XmlDoc::setSpiderStatusDocMetaList ( SafeBuf *jd , int64_t uqd ) {

	// the posdb table
	HashTableX tt4;
	if ( !tt4.set(18,4,256,NULL,0,false,m_niceness,"posdb-spindx"))
		return false;


	Json jp2;
	if (! jp2.parseJsonStringIntoJsonItems (jd->getBufStart(),m_niceness)){
		g_errno = EBADJSONPARSER;
		return false;
	}

	// re-set to 0
	m_dist = 0;

	// hash like gbstatus:"Tcp Timed out" or gbstatus:"Doc unchanged"
	HashInfo hi;
	hi.m_hashGroup = HASHGROUP_INTAG;
	hi.m_tt = &tt4;
	hi.m_desc = "json spider status object";
	hi.m_useCountTable = false;
	hi.m_useSections = false;

	// fill up tt4. false -> do not hash without field prefixes.
	hashJSONFields2 ( &tt4 , &hi , &jp2 , false );

	// store keys in safebuf then to make our own meta list
	addTable144 ( &tt4 , uqd , &m_spiderStatusDocMetaList );

	return true;
}


// returns false and sets g_errno on error
bool XmlDoc::hashMetaTags ( HashTableX *tt ) {

	setStatus ( "hashing meta tags" );

	// assume it's empty
	char buf [ 32*1024 ];
	int32_t bufLen = 32*1024 - 1;
	buf[0] = '\0';
	int32_t     n     = m_xml.getNumNodes();
	XmlNode *nodes = m_xml.getNodes();

	// set up the hashing parms
	HashInfo hi;
	hi.m_hashGroup = HASHGROUP_INMETATAG;
	hi.m_tt        = tt;
	hi.m_desc      = "custom meta tag";

	// find the first meta summary node
	for ( int32_t i = 0 ; i < n ; i++ ) {
		// continue if not a meta tag
		if ( nodes[i].m_nodeId != TAG_META ) continue;
		// only get content for <meta name=..> not <meta http-equiv=..>
		int32_t tagLen;
		char *tag = m_xml.getString ( i , "name" , &tagLen );
		char *tptr = tag;
		char tagLower[128];
		int32_t j ;
		int32_t code;
		// skip if empty
		if ( ! tag || tagLen <= 0 ) continue;
		// make tag name lower case and do not allow bad chars
		if ( tagLen > 126 ) tagLen = 126 ;
		to_lower3_a ( tag , tagLen , tagLower );
		for ( j = 0 ; j < tagLen ; j++ ) {
			// bail if has unacceptable chars
			if ( ! is_alnum_a ( tag[j] ) &&
			     tag[j] != '-' &&
			     tag[j] != '_' &&
			     tag[j] != '.' ) break;
			// convert to lower
			tagLower[j] = to_lower_a ( tag[j] );
		}
		// skip this meta if had unacceptable chars
		if ( j < tagLen ) continue;
		// is it recognized?
		code = getFieldCode ( tag , tagLen );
		// after version 45 or more, do not allow gbrss
		// meta tags, because those are now reserved for us
		if ( code == FIELD_GBRSS ) continue;
		// allow gbrss: fields for earlier versions though
		if ( code == FIELD_GBRSS ) code = FIELD_GENERIC;
		// . do not allow reserved tag names
		// . title,url,suburl,
		if ( code != FIELD_GENERIC ) continue;
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


		// . don't allow reserved names: site, url, suburl, link and ip
		// . actually, the colon is included as part of those
		//   field names, so we really lucked out...!
		// . index this converted tag name
		tptr = tagLower;

		// get the content
		int32_t len;
		char *s = m_xml.getString ( i , "content" , &len );
		if ( ! s || len <= 0 ) continue;
		// . ensure not too big for our buffer (keep room for a \0)
		// . TODO: this is wrong, should be len+1 > bufLen,
		//   but can't fix w/o resetting the index (COME BACK HERE
		//   and see where we index meta tags besides this place!!!)
		//   remove those other places, except... what about keywords
		//   and description?
		if ( len+1 >= bufLen ) {
			//len = bufLen - 1;
			// assume no punct to break on!
			len = 0;
			// only cut off at punctuation
			char *p    = s;
			char *pend = s + len;
			char *last = NULL;
			int32_t  size ;
			for ( ; p < pend ; p += size ) {
				// skip if utf8 char
				size = getUtf8CharSize(*p);
				// skip if 2+ bytes
				if ( size > 1 ) continue;
				// skip if not punct
				if ( is_alnum_a(*p) ) continue;
				// mark it
				last = p;
			}
			if ( last ) len = last - s;
			// this old way was faster...:
			//while ( len > 0 && is_alnum(s[len-1]) ) len--;
		}
		// convert html entities to their chars
		len = saftenTags ( buf , bufLen , s , len );
		// NULL terminate the buffer
		buf[len] = '\0';

		// temp null term
		char c = tptr[tagLen];
		tptr[tagLen] = 0;
		
		
		// BR 20160220
		// Now index the wanted meta tags as normal text without prefix so they
		// are used in user searches automatically.
		// custom
		//hi.m_prefix = tptr;
		hi.m_prefix = NULL;

		// desc is NULL, prefix will be used as desc
		bool status = hashString ( buf,len,&hi );
		// put it back
		tptr[tagLen] = c;
		// bail on error, g_errno should be set
		if ( ! status ) return false;

		// return false with g_errno set on error
		//if ( ! hashNumberForSorting ( buf , bufLen , &hi ) )
		//	return false;
	}

	return true;
}



// . hash dates for sorting by using gbsortby: and gbrevsortby:
// . do 'gbsortby:gbspiderdate' as your query to see this in action
bool XmlDoc::hashDateNumbers ( HashTableX *tt ) { // , bool isStatusDoc ) {

	// stop if already set
	if ( ! m_spideredTimeValid ) return true;

	int32_t indexedTime = getIndexedTime();

	// first the last spidered date
	HashInfo hi;
	hi.m_hashGroup = 0;// this doesn't matter, it's a numeric field
	hi.m_tt        = tt;
	hi.m_desc      = "last spidered date";
	hi.m_prefix    = "gbspiderdate";
	hi.m_createSortByForNumbers = true;

	char buf[64];
	int32_t bufLen = sprintf ( buf , "%"UINT32"", (uint32_t)m_spideredTime );
	if ( ! hashNumberForSorting( buf , buf , bufLen , &hi ) )
		return false;

	// and index time is >= spider time, so you want to sort by that for
	// the widget for instance
 	hi.m_desc      = "last indexed date";
 	hi.m_prefix    = "gbindexdate";
 	bufLen = sprintf ( buf , "%"UINT32"", (uint32_t)indexedTime );
 	if ( ! hashNumberForSorting ( buf , buf , bufLen , &hi ) )
 		return false;

	// do not index the rest if we are a "spider reply" document
	// which is like a fake document for seeing spider statuses
	//if ( isStatusDoc == CT_STATUS ) return true;
	//if ( isStatusDoc ) return true;

	// now for CT_STATUS spider status "documents" we also index
	// gbspiderdate so index this so we can just do a
	// gbsortby:gbdocspiderdate and only get real DOCUMENTS not the
	// spider status "documents"
/*
  BR 20160108: Don't store these as we don't plan to use them
	hi.m_desc      = "doc last spidered date";
	hi.m_prefix    = "gbdocspiderdate";
	bufLen = sprintf ( buf , "%"UINT32"", (uint32_t)m_spideredTime );
	if ( ! hashNumberForSorting ( buf , buf , bufLen , &hi ) )
		return false;

 	hi.m_desc      = "doc last indexed date";
 	hi.m_prefix    = "gbdocindexdate";
 	bufLen = sprintf ( buf , "%"UINT32"", (uint32_t)indexedTime );
 	if ( ! hashNumberForSorting ( buf , buf , bufLen , &hi ) )
 		return false;
*/

	// all done
	return true;
}

// returns false and sets g_errno on error
bool XmlDoc::hashContentType ( HashTableX *tt ) {

	CollectionRec *cr = getCollRec();
	if ( ! cr ) return false;

	uint8_t ctype = *getContentType();
	char *s = NULL;

	setStatus ( "hashing content type" );


	// hash numerically so we can do gbfacetint:type on it
	HashInfo hi;
	hi.m_hashGroup = HASHGROUP_INTAG;
	hi.m_tt        = tt;
	hi.m_prefix    = "type";

	char tmp[6];
	sprintf(tmp,"%"UINT32"",(uint32_t)ctype);
	if ( ! hashString (tmp,gbstrlen(tmp),&hi ) ) return false;


	// these ctypes are defined in HttpMime.h
	switch (ctype) {
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

	// hack for diffbot. do not hash type:json because diffbot uses
	// that for searching diffbot json objects
	if ( cr->m_isCustomCrawl && ctype==CT_JSON )
		return true;

	// . now hash it
	// . use a score of 1 for all
	// . TODO: ensure doc counting works ok with this when it does
	//   it's interpolation
	return hashString (s,gbstrlen(s),&hi );
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
	Url *cu = getCurrentUrl() ;
	Url *ru = *getRedirUrl() ;

	char dbuf[8*4*1024];
	HashTableX dedup;
	dedup.set( 8,0,1024,dbuf,8*4*1024,false,m_niceness,"hldt");

	// see ../url/Url2.cpp for hashAsLink() algorithm
	for ( int32_t i = 0 ; i < m_links.m_numLinks ; i++ ) {
		// skip links with zero 0 length
		if ( m_links.m_linkLens[i] == 0 ) continue;
		// . skip if we are rss page and this link is an <a href> link
		// . we only harvest/index <link> urls from rss feeds
		// . or in the case of feedburner, those orig tags
		if ( isRSSFeed && (m_links.m_linkFlags[i] & LF_AHREFTAG) )
			continue;
		// if we have a <feedburner:origLink> tag, then ignore <link>
		// tags and only get the links from the original links
		if ( m_links.m_isFeedBurner &&
		     !(m_links.m_linkFlags[i] & LF_FBTAG) )
			continue;
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
		link.set( m_links.m_linkPtrs[i], m_links.m_linkLens[i], true, m_links.m_stripIds, true ); // m_version (add here if needed again)


		// BR 20160105: Do not create "link:" hashes for media URLs etc.
		if( link.hasNonIndexableExtension(TITLEREC_CURRENT_VERSION) ||	// @todo BR: For now ignore actual TitleDB version. // m_version) ||
			link.hasScriptExtension() ||
			link.hasJsonExtension() ||
			link.hasXmlExtension() ||
			link.isDomainUnwantedForIndexing() ||
			link.isPathUnwantedForIndexing() )
		{
			if( g_conf.m_logTraceXmlDoc ) log(LOG_TRACE,"%s:%s:%d: Unwanted for indexing [%s]", __FILE__, __func__, __LINE__, link.getUrl());
			continue;			
		}


		// breathe
		QUICKPOLL(m_niceness);
		// . the score depends on some factors:
		// . NOTE: these are no longer valid! (see score bitmap above)
		// . 4 --> if link has different domain AND has link text
		// . 3 --> if link has same domain AND has link text
		// . 2 --> if link has different domain AND no link text
		// . 1 --> if link has sam domain AND no link text
		// . is domain the same as ours?
		// . NOTE: ideally, using the IP domain would be better, but
		//   we do not know the ip of the linker right now... so scores
		//   may be topped with a bunch of same-ip domain links so that
		//   we may not get as much link text as we'd like, since we
		//   only sample from one link text per ip domain
		// . now we also just use the mid domain! (excludes TLD)
		bool internal = false;
		int32_t mdlen = cu->getMidDomainLen();
		if ( mdlen == link.getMidDomainLen() &&
		     strncmp(cu->getMidDomain(),link.getMidDomain(),mdlen)==0)
			//continue; // sameMidDomain = true;
			internal = true;
		// also check the redir url
		if ( ru ) {
			mdlen = ru->getMidDomainLen();
			if ( mdlen == link.getMidDomainLen() &&
			     strncmp(ru->getMidDomain(), link.getMidDomain(),mdlen)==0)
				//continue; // sameMidDomain = true;
				internal = true;
		}
		// now make the score
		//unsigned char score ;
		// . TODO: consider not hashing link w/o text!
		// . otherwise, give it a higher score if it's got link TEXT
		//bool gotLinkText = m_links.hasLinkText ( i, m_version );
		// otherwise, beginning with version 21, allow internal links,
		// but with lower scores
		//                          score
		// internal, no link text:  2
		// internal, w/ link text:  4
		// external, no link text:  6
		// external, w/ link text:  8
		//if ( internal ) {
		//	if ( ! gotLinkText ) score = 0x02;
		//	else                 score = 0x04;
		//}
		//else {
		//	if ( ! gotLinkText ) score = 0x06;
		//	else                 score = 0x08;
		//}


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
		if ( ! hashSingleTerm ( link.getUrl(),link.getUrlLen(),&hi ))
			return false;


		h = hash64 ( link.getHost() , link.getHostLen() );
		if ( dedup.isInTable ( &h ) ) continue;
		if ( ! dedup.addKey ( &h ) ) return false;


		// fix parm
		hi.m_prefix    = "sitelink";

		// hash sitelink:<urlHost>
		if ( ! hashSingleTerm ( link.getHost(),link.getHostLen(),&hi))
			return false;
		// breathe
		QUICKPOLL(m_niceness);
	}

	// skip this for now
	return true;

	/*
	setStatus ("hashing gbhasbannedoutlink" );

	// only lets a domain vote once
	int32_t numBannedOutlinks = *getNumBannedOutlinks();
	//if ( numBannedOutlinks <= 0 ) return true;
	// a score of 235 seems to give a negative return for score8to32()
	uint32_t score = score8to32 ( numBannedOutlinks );
	// make score at least 1!
	if ( score <= 0 ) score = 1;
	// a hack fix
	if ( score > 0x7fffffff ) score = 0x7fffffff;

	// set up the hashing parms
	HashInfo hi;
	hi.m_tt        = tt;
	hi.m_prefix    = "gbhasbannedoutlink";

	// hash this special thing to help us de-spam the index
	if ( numBannedOutlinks > 0 ) return hashString ("1",1,&hi );
	else                         return hashString ("0",1,&hi );
	*/
}


// . returns false and sets g_errno on error
// . hash for linkdb
bool XmlDoc::hashLinksForLinkdb ( HashTableX *dt ) {

	// sanity check
	if ( dt->m_ks != sizeof(key224_t) ) { char *xx=NULL;*xx=0; }
	if ( dt->m_ds != 0                ) { char *xx=NULL;*xx=0; }

	// this will be different with our new site definitions
	uint32_t linkerSiteHash32 = *getSiteHash32();

	char siteRank = getSiteRank();

	if ( ! m_linksValid ) { char *xx=NULL;*xx=0; }

	// we need to store this in the title rec for re-building
	// the meta list from the title rec...
	// is this just site info?
	//TagRec ***pgrv = getOutlinkTagRecVector();
	//if ( ! pgrv || pgrv == (void *)-1 ) { char *xx=NULL;*xx=0; }
	//TagRec **grv = *pgrv;

	int32_t *linkSiteHashes = getLinkSiteHashes();
	if ( ! linkSiteHashes || linkSiteHashes == (void *)-1 ){
		char *xx=NULL;*xx=0;}

	// convert siteNumInlinks into a score
	//int32_t numSiteInlinks = *xd->getSiteNumInlinks();

	unsigned char hopCount = *getHopCount();

	// use spidered time! might not be current time! like if rebuilding
	// or injecting from a past spider time
	int32_t discoveryDate = getSpideredTime();//TimeGlobal();
	int32_t lostDate      = 0;

	// add in new links
	for ( int32_t i = 0 ; i < m_links.m_numLinks ; i++ ) {
		// give up control
		QUICKPOLL ( m_niceness );
		// skip if empty
		if ( m_links.m_linkLens[i] == 0 ) continue;
		// . skip if spam, ALWAYS allow internal outlinks though!!
		// . CAUTION: now we must version islinkspam()
		bool spam = m_links.isLinkSpam(i) ;
		// or if it has no link text, skip it
		//if ( ! links->hasLinkText(i,TITLEREC_CURRENT_VERSION) )
		//continue;
		// get site of outlink from tagrec if in there
		int32_t linkeeSiteHash32 = linkSiteHashes[i];
		/*
		TagRec *gr = grv[i];
		char *site = NULL;
		int32_t  siteLen = 0;
		if (   gr   ) {
			int32_t dataSize = 0;
			site = gr->getString("site",NULL,&dataSize);
			if ( dataSize ) siteLen = dataSize - 1;
		}
		// otherwise, make it the host or make it cut off at
		// a "/user/" or "/~xxxx" or whatever path component
		if ( ! site ) {
			// GUESS link site... TODO: augment for /~xxx
			char *s = m_links.getLink(i);
			//int32_t  slen = m_links.getLinkLen(i);
			//siteLen = slen;
			site = ::getHost ( s , &siteLen );
		}
		uint32_t linkeeSiteHash32 = hash32 ( site , siteLen , 0 );
		*/

		//
		// when setting the links class it should set the site hash
		//


#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(&linkeeSiteHash32,sizeof(linkeeSiteHash32));
	uint64_t tmp1 = m_links.getLinkHash64(i);
	VALGRIND_CHECK_MEM_IS_DEFINED(&tmp1,sizeof(tmp1));
	VALGRIND_CHECK_MEM_IS_DEFINED(&spam,sizeof(spam));
	VALGRIND_CHECK_MEM_IS_DEFINED(&siteRank,sizeof(siteRank));
	VALGRIND_CHECK_MEM_IS_DEFINED(&hopCount,sizeof(hopCount));
	uint32_t tmp2 = *getIp();
	VALGRIND_CHECK_MEM_IS_DEFINED(&tmp2,sizeof(tmp2));
	uint64_t tmp3 = *getDocId();
	VALGRIND_CHECK_MEM_IS_DEFINED(&tmp3,sizeof(tmp3));
	VALGRIND_CHECK_MEM_IS_DEFINED(&discoveryDate,sizeof(discoveryDate));
	VALGRIND_CHECK_MEM_IS_DEFINED(&lostDate,sizeof(lostDate));
	VALGRIND_CHECK_MEM_IS_DEFINED(&linkerSiteHash32,sizeof(linkerSiteHash32));
#endif
		// set this key, it is the entire record
		key224_t k;
		k = g_linkdb.makeKey_uk ( linkeeSiteHash32 ,
					  m_links.getLinkHash64(i)   ,
					  spam               , // link spam?
					  siteRank     , // was quality
					  hopCount,
					  *getIp()       ,
					  *getDocId()    ,
					  discoveryDate      ,
					  lostDate           ,
					  false              , // new add?
					  linkerSiteHash32   ,
					  false              );// delete?
#ifdef _VALGRIND_
	VALGRIND_CHECK_MEM_IS_DEFINED(&k,sizeof(k));
#endif
		/*
		// debug
		if ( m_links.getLinkHash64(i) != 0x3df1c439a364e18dLL )
			continue;
		//char c = site[siteLen];
		//site[siteLen]=0;
		//char tmp[1024];
		//sprintf(tmp,"xmldoc: hashinglink site=%s sitelen=%"INT32" ",
		//	site,siteLen);
		//site[siteLen] = c;
		log(//"%s "
		    "url=%s "
		    "linkeesitehash32=0x%08"XINT32" "
		    "linkersitehash32=0x%08"XINT32" "
		    "urlhash64=0x%16llx "
		    "docid=%"INT64" k=%s",
		    //tmp,
		    m_links.getLink(i),
		    (int32_t)linkeeSiteHash32,
		    linkerSiteHash32,
		    m_links.getLinkHash64(i),
		    *getDocId(),
		    KEYSTR(&k,sizeof(key224_t))
		    );
		*/
		// store in hash table
		if ( ! dt->addKey ( &k , NULL ) ) return false;
	}
	return true;
}

bool XmlDoc::getUseTimeAxis ( ) {
	if ( m_useTimeAxisValid )
		return m_useTimeAxis;
	if ( m_setFromTitleRec )
		// return from titlerec header
		return m_useTimeAxis;
	CollectionRec *cr = g_collectiondb.getRec ( m_collnum );
	if ( ! cr ) return false;
	m_useTimeAxis = cr->m_useTimeAxis;
	m_useTimeAxisValid = true;
	return m_useTimeAxis;
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
	uw.set( fu->getUrl(), fu->getUrlLen(), true, false, false );
	hi.m_prefix    = "url";
	// no longer, we just index json now
	//if ( isStatusDoc ) hi.m_prefix = "url2";
	if ( ! hashSingleTerm(uw.getUrl(),uw.getUrlLen(),&hi) )
		return false;

	if( urlOnly )
	{
		return true;
	}


	if ( getUseTimeAxis() ) { // g_conf.m_useTimeAxis ) {
		hi.m_prefix = "gbtimeurl";
		SafeBuf *tau = getTimeAxisUrl();
		hashSingleTerm ( tau->getBufStart(),tau->length(),&hi);
	}

	setStatus ( "hashing inurl colon" );

	//
	// HASH inurl: terms
	//
	char *s    = fu->getUrl   ();
	int32_t  slen = fu->getUrlLen();
	hi.m_prefix = "inurl";


	// BR 20160114: Skip numbers in urls when doing "inurl:" queries
	hi.m_hashNumbers = false;
	hi.m_hashCommonWebWords = false;
	if ( ! hashString ( s,slen, &hi ) ) return false;


	setStatus ( "hashing ip colon" );
	hi.m_hashNumbers = true;
	hi.m_hashCommonWebWords = true;

	//
	// HASH ip:a.b.c.d
	//
	if ( ! m_ipValid ) { char *xx=NULL;*xx=0; }
	// copy it to save it
	char ipbuf[64];
	int32_t iplen = sprintf(ipbuf,"%s",iptoa(m_ip));
	hi.m_prefix = "ip";
	if ( ! hashSingleTerm(ipbuf,iplen,&hi) ) return false;

	// . sanity check
	if ( ! m_siteNumInlinksValid ) { char *xx=NULL;*xx=0; }

	char buf[20];
	int32_t blen;

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
		pbufLen=snprintf(pbuf, 12, ":%"UINT32"",fu->getPort());
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
		hi.m_prefix    = "site";
		// no longer, we just index json now
		//if ( isStatusDoc ) hi.m_prefix = "site2";
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
	
		if( dotpos && ( (slashpos && slashpos > dotpos) || !slashpos) )
		{
			dom_valid = true;
		}
	}
	
	if ( name < end3 && dom_valid ) goto loop;



	// BR 20160121: Make searching for e.g. site:dk work
	setStatus ( "hashing tld for site search");
	const char *tld = fu->getTLD();
	int32_t tldLen = fu->getTLDLen();
	
	char *p = buf;
	gbmemcpy ( p , "http://", 7 ); p += 7;
	gbmemcpy ( p , tld, tldLen); p += tldLen;
	gbmemcpy ( p , "/", 1 ); p += 1;
	*p = '\0';
	if ( ! hashSingleTerm (buf,p-buf,&hi ) ) return false;


	//
	// HASH ext: term
	//
	// i.e. ext:gif ext:html ext:htm ext:pdf, etc.
	setStatus ( "hashing ext colon");
	const char *ext  = fu->getExtension();
	int32_t  elen = fu->getExtensionLen();
	// update hash parms
	hi.m_prefix    = "ext";
	// no longer, we just index json now
	//if ( isStatusDoc ) hi.m_prefix = "ext2";
	if ( ! hashSingleTerm(ext,elen,&hi ) ) return false;


	setStatus ( "hashing gbdocid" );
	hi.m_prefix = "gbdocid";
	// no longer, we just index json now
	//if ( isStatusDoc ) hi.m_prefix = "gbdocid2";
	char buf2[32];
	sprintf(buf2,"%"UINT64"",(m_docId) );
	if ( ! hashSingleTerm(buf2,gbstrlen(buf2),&hi) ) return false;

	//if ( isStatusDoc ) return true;

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
	if ( ! m_linksValid ) { char *xx=NULL; *xx=0; }
	// . skip if we have no subdirectory outlinks
	// . that way we do not confuse all the pages in dictionary.com or
	//   wikipedia.org as subsites!!
	if ( ! m_links.hasSubdirOutlink() ) add = false;

	char *host = fu->getHost        ();
	int32_t  hlen = fu->getHostLen     ();

	// tags from here out
	hi.m_hashGroup = HASHGROUP_INTAG;
	hi.m_shardByTermId = true;
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
	hi.m_shardByTermId  = false;

	setStatus ( "hashing urlhashdiv10 etc");

	//
	// HASH urlhash: urlhashdiv10: urlhashdiv100: terms
	//
	// this is for proving how many docs are in the index
	uint32_t h = hash32 ( s , slen );
	blen = sprintf(buf,"%"UINT32"",h);
	hi.m_prefix    = "urlhash";
	if ( ! hashString(buf,blen,&hi) ) return false;

/*
	BR 20160106 removed.
	blen = sprintf(buf,"%"UINT32"",h/10);
	// update hashing parms
	hi.m_prefix = "urlhashdiv10";
	if ( ! hashString(buf,blen,&hi) ) return false;
	blen = sprintf(buf,"%"UINT32"",h/100);
	// update hashing parms
	hi.m_prefix = "urlhashdiv100";
	if ( ! hashString(buf,blen,&hi) ) return false;
*/


	setStatus ( "hashing url mid domain");

	// update parms
	hi.m_prefix    = NULL;
	hi.m_desc      = "middle domain";
	hi.m_hashGroup = HASHGROUP_INURL;
	hi.m_hashCommonWebWords = false;	// Skip www, com, http etc.
	if ( ! hashString ( host,hlen,&hi)) return false;

	hi.m_hashCommonWebWords = true;
	if ( ! hashSingleTerm ( fu->getDomain(),fu->getDomainLen(),&hi)) return false;


	setStatus ( "hashing url path");
	char *path = fu->getPath();
	int32_t  plen = fu->getPathLen();

	// BR 20160113: Do not hash and combine the page filename extension with the page name (skip e.g. .com)
	if( elen > 0 )
	{
		elen++;	// also skip the dot
	}
	plen -= elen;


	// BR 20160113: Do not hash the most common page names
	if( strncmp(path, "/index", plen) != 0 )
	{
		// hash the path
		// BR 20160114: Exclude numbers in paths (usually dates)
		hi.m_hashNumbers = false;
		if ( ! hashString (path,plen,&hi) ) return false;
	}

	return true;
}

// . returns false and sets g_errno on error
bool XmlDoc::hashIncomingLinkText ( HashTableX *tt               ,
				    bool        hashAnomalies    ,
				    bool        hashNonAnomalies ) {

	// do not index ANY of the body if it is NOT a permalink and
	// "menu elimination" technology is enabled.
	//if ( ! *getIsPermalink() && m_eliminateMenus ) return true;

	setStatus ( "hashing link text" );

	// . now it must have an rss item to be indexed in all its glory
	// . but if it tells us it has an rss feed, toss it and wait for
	//   the feed.... BUT sometimes the rss feed outlink is 404!
	// . NO, now we discard with ENORSS at Msg16.cpp
	//if ( ! *getHasRSSItem() &&  m_eliminateMenus ) return true;

	// sanity check
	if ( hashAnomalies == hashNonAnomalies ) { char *xx = NULL; *xx =0; }
	// display this note in page parser
	char *note = "hashing incoming link text";
	// sanity
	if ( ! m_linkInfo1Valid ) { char *xx=NULL;*xx=0; }

	// . finally hash in the linkText terms from the LinkInfo
	// . the LinkInfo class has all the terms of hashed anchor text for us
	// . if we're using an old TitleRec linkTermList is just a ptr to
	//   somewhere in TitleRec
	// . otherwise, we generated it from merging a bunch of LinkInfos
	//   and storing them in this new TitleRec
	LinkInfo  *info1    = getLinkInfo1 ();
	LinkInfo  *linkInfo = info1;

	// sanity checks
	if ( ! m_ipValid             ) { char *xx=NULL;*xx=0; }
	if ( ! m_siteNumInlinksValid ) { char *xx=NULL;*xx=0; }

	//
	// brought the following code in from LinkInfo.cpp
	//

	int32_t noteLen = 0;
	if ( note ) noteLen = gbstrlen ( note );
	// count "external" inlinkers
	int32_t ecount = 0;

	// update hash parms
	HashInfo hi;
	hi.m_tt        = tt;
	// hashstring should update this like a cursor.
	hi.m_startDist = 0;

	// loop through the link texts and hash them
	for ( Inlink *k = NULL; (k = linkInfo->getNextInlink(k)) ; ) {
		// is this inlinker internal?
		bool internal=((m_ip&0x0000ffff)==(k->m_ip&0x0000ffff));
		// count external inlinks we have for indexing gbmininlinks:
		if ( ! internal ) ecount++;
		// get score
		//int64_t baseScore = k->m_baseScore;
                // get the weight
		//int64_t ww ;
		//if ( internal ) ww = m_internalLinkTextWeight;
		//else            ww = m_externalLinkTextWeight;
		// modify the baseScore
		//int64_t final = (baseScore * ww) / 100LL;
		// get length of link text
		int32_t tlen = k->size_linkText;
		if ( tlen > 0 ) tlen--;
		// get the text
		char *txt = k->getLinkText();
		// sanity check
		if ( ! verifyUtf8 ( txt , tlen ) ) {
			log("xmldoc: bad link text 2 from url=%s for %s",
			    k->getUrl(),m_firstUrl.getUrl());
			continue;
		}
		// if it is anomalous, set this, we don't
		//if ( k->m_isAnomaly )
		//	hi.m_hashIffNotUnique = true;
		//hi.m_baseScore = final;
		if ( internal ) hi.m_hashGroup = HASHGROUP_INTERNALINLINKTEXT;
		else            hi.m_hashGroup = HASHGROUP_INLINKTEXT;
		// store the siterank of the linker in this and use that
		// to set the multiplier M bits i guess
		hi.m_linkerSiteRank = k->m_siteRank;
		// now record this so we can match the link text to
		// a matched offsite inlink text term in the scoring info
		k->m_wordPosStart = m_dist; // hi.m_startDist;
		// . hash the link text into the table
		// . returns false and sets g_errno on error
		// . we still have the score punish from # of words though!
		// . for inlink texts that are the same it should accumulate
		//   and use the reserved bits as a multiplier i guess...
		if ( ! hashString ( txt,tlen,&hi) ) return false;
		// now record this so we can match the link text to
		// a matched offsite inlink text term in the scoring info
		//k->m_wordPosEnd = hi.m_startDist;
		// spread it out
		hi.m_startDist += 20;
	}

	/*
	// . hash gbkeyword:numinlinks where score is # of inlinks from 1-255
	// . do not hash gbkeyword:numinlinks if we don't got any
	if ( ecount <= 0 ) return true;
	// limit it since our score can't be more than 255 (8-bits)
	//if ( ecount > 255 ) ecount = 255;
	// convert our 32 bit score to 8-bits so we trick it!
	//int32_t score = score8to32 ( (uint8_t)ecount );
	// watch out for wrap
	//if ( score < 0 ) score = 0x7fffffff;
	// update hash parms
	HashInfo hi;
	hi.m_tt        = tt;
	hi.m_prefix    = "gbkeyword";
	hi.m_hashGroup = HASHGROUP_INTAG;
	// for terms where word position/density/diversity is irrelevant,
	// we can store this value...
	hi.m_fakeValue = ecount;
	// hash gbkeyword:numinlinks term
	if ( ! hashString ( "numinlinks",10,&hi ) )return false;
	*/

	return true;
}

// . returns false and sets g_errno on error
bool XmlDoc::hashNeighborhoods ( HashTableX *tt ) {

	// seems like iffUnique is off, so do this
	//if ( ! *getIsPermalink() && m_eliminateMenus ) return true;

	setStatus ( "hashing neighborhoods" );

	//g_tt = table;

	// . now we also hash the neighborhood text of each inlink, that is,
	//   the text surrounding the inlink text.
	// . this is also destructive in that it will remove termids that
	//   were not in the document being linked to in order to save
	//   space in the titleRec
	// . now we only do one or the other, not both
	LinkInfo  *info1    = getLinkInfo1 ();
	LinkInfo  *linkInfo = info1;

	// loop over all the Inlinks
	Inlink *k = NULL;
 loop:
	// get the next inlink
	k = linkInfo->getNextInlink( k );
	// break if done
	if ( ! k ) return true;

	// skip if internal, they often have the same neighborhood text
	if ( (k->m_ip&0x0000ffff)==(m_ip&0x0000ffff) ) goto loop;

	// get the left and right texts and hash both
	char *s = k->getSurroundingText();
	if ( ! s || k->size_surroundingText <= 1 ) goto loop;

	//int32_t inlinks = *getSiteNumInlinks();

	// HACK: to avoid having to pass a flag to TermTable, then to
	// Words::hash(), Phrases::hash(), etc. just flip a bit in the
	// table to make it not add anything unless it is already in there.
	tt->m_addIffNotUnique = true;

	// update hash parms
	HashInfo hi;
	hi.m_tt        = tt;
	hi.m_desc      = "surrounding text";
	hi.m_hashGroup = HASHGROUP_NEIGHBORHOOD;

	// . hash that
	// . this returns false and sets g_errno on error
	int32_t len = k->size_surroundingText - 1;
	if ( ! hashString ( s, len, &hi ) ) return false;

	// now turn it back off
	tt->m_addIffNotUnique = false;

	// get the next Inlink
	goto loop;
}

// . we now do the title hashing here for newer titlerecs, version 80+, rather
//   than use the <index> block in the ruleset for titles.
// . this is not to be confused with hashing the title: terms which still
//   does have an <index> block in the ruleset.
// . the new Weights class hashes title as part of body now with a high weight
//   given by "titleWeight" parm
bool XmlDoc::hashTitle ( HashTableX *tt ) {
	// sanity check
	if ( m_hashedTitle ) { char *xx=NULL ; *xx=0; }

	setStatus ( "hashing title" );

	// this has been called, note it
	m_hashedTitle = true;

	nodeid_t *tids = m_words.m_tagIds;
	int32_t      nw   = m_words.m_numWords;

	// find the first <title> tag in the doc
	int32_t i ;
	for ( i = 0 ; i < nw ; i++ )
		if ( tids[i] == TAG_TITLE ) break;

	// return true if no title
	if ( i >= nw ) return true;

	// skip tag
	i++;
	// mark it as start of title
	int32_t a = i;

	// limit end
	int32_t max = i + 40;
	if ( max > nw ) max = nw;

	// find end of title, either another <title> or a <title> tag
	for ( ; i < max ; i++ )
		if ( (tids[i] & BACKBITCOMP) == TAG_TITLE ) break;

	// ends on a <title> tag?
	if ( i == a ) return true;

	HashInfo hi;
	hi.m_tt        = tt;
	hi.m_prefix    = "title";

	// the new posdb info
	hi.m_hashGroup      = HASHGROUP_TITLE;

	// . hash it up! use 0 for the date
	// . use XmlDoc::hashWords()
	// . use "title" as both prefix and description
	//if ( ! hashWords (a,i,&hi ) ) return false;

	char **wptrs = m_words.getWords();
	int32_t  *wlens = m_words.getWordLens();
	char  *title    = wptrs[a];
	char  *titleEnd = wptrs[i-1] + wlens[i-1];
	int32_t   titleLen = titleEnd - title;
	if ( ! hashString ( title, titleLen, &hi) ) return false;

	// now hash as without title: prefix
	hi.m_prefix = NULL;
	if ( ! hashString ( title, titleLen, &hi) ) return false;

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

	// if more than X% of words are spammed to some degree, index all
	// words with a minimum score
	//int64_t x[] = {30,40,50,70,90};
	//int64_t y[] = {6,8,10,20,30};
	//int32_t mp = getY ( *getSiteNumInlinks8() , x , y , 5 );

	//int32_t nw = m_words.getNumWords();

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
	char *mk = getMetaKeywords( &mklen );

	// update hash parms
	HashInfo hi;
	hi.m_tt         = tt;
	hi.m_desc       = "meta keywords";
	hi.m_hashGroup  = HASHGROUP_INMETATAG;

	// call XmlDoc::hashString
	return hashString ( mk , mklen , &hi);
}


// . hash the meta summary, description and keyword tags
// . we now do the title hashing here for newer titlerecs, version 80+, rather
//   than use the <index> block in the ruleset for titles.
bool XmlDoc::hashMetaSummary ( HashTableX *tt ) {

	// sanity check
	if ( m_hashedMetas ) { char *xx=NULL ; *xx=0; }

	// this has been called, note it
	m_hashedMetas = true;

	// do not index meta tags if "menu elimination" technology is enabled.
	//if ( m_eliminateMenus ) return true;

	setStatus ( "hashing meta summary" );

	// hash the meta keywords tag
	//char buf [ 2048 + 2 ];
	//int32_t len = m_xml.getMetaContent ( buf , 2048 , "summary" , 7 );
	int32_t mslen;
	char *ms = getMetaSummary ( &mslen );

	// update hash parms
	HashInfo hi;
	hi.m_tt         = tt;
	hi.m_hashGroup  = HASHGROUP_INMETATAG;

	// udpate hashing parms
	hi.m_desc = "meta summary";
	// hash it
	if ( ! hashString ( ms , mslen , &hi )) return false;


	//len = m_xml.getMetaContent ( buf , 2048 , "description" , 11 );
	int32_t mdlen;
	char *md = getMetaDescription ( &mdlen );

	// udpate hashing parms
	hi.m_desc = "meta desc";
	// . TODO: only hash if unique????? set a flag on ht then i guess
	if ( ! hashString ( md , mdlen , &hi ) ) return false;

	return true;
}


bool XmlDoc::hashMetaGeoPlacename( HashTableX *tt ) {

	setStatus ( "hashing meta geo.placename" );

	int32_t mgplen;
	char *mgp = getMetaGeoPlacename( &mgplen );

	// update hash parms
	HashInfo hi;
	hi.m_tt         = tt;
	hi.m_desc       = "meta geo.placename";
	hi.m_hashGroup  = HASHGROUP_INMETATAG;

	// call XmlDoc::hashString
	return hashString ( mgp , mgplen , &hi);
}




bool XmlDoc::hashLanguage ( HashTableX *tt ) {

	setStatus ( "hashing language" );

	int32_t langId = (int32_t)*getLangId();

	char s[32]; // numeric langid
	int32_t slen = sprintf(s, "%"INT32"", langId );

	// update hash parms
	HashInfo hi;
	hi.m_tt        = tt;
	hi.m_hashGroup = HASHGROUP_INTAG;
	hi.m_prefix    = "gblang";

	if ( ! hashString ( s, slen, &hi ) ) return false;

/* 
	BR 20160117: Duplicate
	// try lang abbreviation
	sprintf(s , "%s ", getLanguageAbbr(langId) );
	// go back to broken way to try to fix parsing consistency bug
	// by adding hashLanguageString() function below
	//sprintf(s , "%s ", getLanguageAbbr(langId) );
	if ( ! hashString ( s, slen, &hi ) ) return false;
*/
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

bool XmlDoc::hashPermalink ( HashTableX *tt ) {

	setStatus ( "hashing is permalink" );

	// put a colon in there so it can't be faked using a meta tag.
	char *s = "0";
	if ( *getIsPermalink() ) s = "1";

	// update hash parms
	HashInfo hi;
	hi.m_tt        = tt;
	hi.m_hashGroup = HASHGROUP_INTAG;
	hi.m_prefix    = "gbpermalink";

	return hashString ( s,1,&hi );
}

// returns false and sets g_errno on error
bool XmlDoc::hashIsAdult ( HashTableX *tt ) {

	setStatus ("hashing isadult");

	char *ia = getIsAdult();
	// this should not block or return error! should have been
	// set in prepareToMakeTitleRec() before hashAll() was called!
	if ( ! ia || ia == (void *)-1 ) {char *xx=NULL;*xx=0; }

	// index gbisadult:1 if adult or gbisadult:0 if not
	char *val;
	if ( *ia ) val = "1";
	else       val = "0";

	// update hash parms
	HashInfo hi;
	hi.m_tt        = tt;
	hi.m_hashGroup = HASHGROUP_INTAG;
	hi.m_prefix    = "gbisadult";
	hi.m_desc      = "is document adult content";

	// this returns false on failure
	if ( ! hashString ( val,1,&hi ) ) return false;

	return true;
}

bool XmlDoc::hashSingleTerm( const char *s, int32_t slen, HashInfo *hi ) {
	// empty?
	if ( slen <= 0 ) return true;
	if ( ! m_versionValid    ) { char *xx=NULL;*xx=0; }
	if ( hi->m_useCountTable && ! m_countTableValid){char *xx=NULL;*xx=0; }

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
	if ( dt->m_ks != sizeof(key144_t) ) { char *xx=NULL;*xx=0; }
	// make the key like we do in hashWords()


	key144_t k;
	g_posdb.makeKey ( &k ,
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

bool XmlDoc::hashString( char *s, int32_t slen, HashInfo *hi ) {
	if ( ! m_versionValid        ) { char *xx=NULL;*xx=0; }

	if ( hi->m_useCountTable && ! m_countTableValid){char *xx=NULL;*xx=0; }

	if ( ! m_siteNumInlinksValid ) { char *xx=NULL;*xx=0; }

	int32_t *sni = getSiteNumInlinks();
	return   hashString3( s                ,
			      slen             ,
			      hi               ,
			      &m_countTable    ,
			      m_pbuf           ,
			      m_wts            ,
			      &m_wbuf          ,
			      m_version        ,
			      *sni             ,
			      m_niceness       );
}


bool XmlDoc::hashString3( char       *s              ,
		  int32_t        slen           ,
		  HashInfo   *hi             ,
		  HashTableX *countTable     ,
		  SafeBuf    *pbuf           ,
		  HashTableX *wts            ,
		  SafeBuf    *wbuf           ,
		  int32_t        version        ,
		  int32_t        siteNumInlinks ,
		  int32_t        niceness       ) {
	Words   words;
	Bits    bits;
	Phrases phrases;

	if ( ! words.set   ( s , slen , true , niceness ) )
		return false;
	if ( !bits.set(&words, niceness))
		return false;
	if ( !phrases.set( &words, &bits, niceness ) )
		return false;

	// use primary langid of doc
	if ( ! m_langIdValid ) { char *xx=NULL;*xx=0; }

	return hashWords3( hi, &words, &phrases, NULL, countTable, NULL, NULL, NULL, wts, wbuf, niceness );
}

bool XmlDoc::hashWords ( HashInfo   *hi ) {
	// sanity checks
	if ( ! m_wordsValid   ) { char *xx=NULL; *xx=0; }
	if ( ! m_phrasesValid ) { char *xx=NULL; *xx=0; }
	if ( hi->m_useCountTable &&!m_countTableValid){char *xx=NULL; *xx=0; }
	if ( ! m_bitsValid ) { char *xx=NULL; *xx=0; }
	if ( ! m_sectionsValid) { char *xx=NULL; *xx=0; }
	//if ( ! m_synonymsValid) { char *xx=NULL; *xx=0; }
	if ( ! m_fragBufValid ) { char *xx=NULL; *xx=0; }
	if ( ! m_wordSpamBufValid ) { char *xx=NULL; *xx=0; }
	if ( m_wts && ! m_langVectorValid  ) { char *xx=NULL; *xx=0; }
	if ( ! m_langIdValid ) { char *xx=NULL; *xx=0; }
	// . is the word repeated in a pattern?
	// . this should only be used for document body, for meta tags,
	//   inlink text, etc. we should make sure words are unique
	char *wordSpamVec = getWordSpamVec();
	char *fragVec = m_fragBuf.getBufStart();
	char *langVec = m_langVec.getBufStart();

	return hashWords3( hi, &m_words, &m_phrases, &m_sections, &m_countTable, fragVec, wordSpamVec, langVec, m_wts,
	                   &m_wbuf, m_niceness );
}

// . this now uses posdb exclusively
bool XmlDoc::hashWords3( HashInfo *hi, Words *words, Phrases *phrases, Sections *sectionsArg, HashTableX *countTable,
                         char *fragVec, char *wordSpamVec, char *langVec, HashTableX *wts, SafeBuf *wbuf,
                         int32_t niceness ) {
	Sections *sections = sectionsArg;
	// for getSpiderStatusDocMetaList() we don't use sections it'll mess us up
	if ( ! hi->m_useSections ) sections = NULL;

	// shortcuts
	uint64_t *wids    = (uint64_t *)words->getWordIds();
	uint64_t *pids2   = (uint64_t *)phrases->getPhraseIds2();

	HashTableX *dt = hi->m_tt;

	// . sanity checks
	// . posdb just uses the full keys with docid
	if ( dt->m_ks != 18 ) { char *xx=NULL;*xx=0; }
	if ( dt->m_ds != 4  ) { char *xx=NULL;*xx=0; }

	// if provided...
	if ( wts ) {
		if ( wts->m_ks != 12               ) { char *xx=NULL;*xx=0; }
		if ( wts->m_ds != sizeof(TermDebugInfo)){char *xx=NULL;*xx=0; }
		if ( ! wts->m_allowDups ) { char *xx=NULL;*xx=0; }
	}

	// ensure caller set the hashGroup
	if ( hi->m_hashGroup < 0 ) { char *xx=NULL;*xx=0; }

	// handy
	char **wptrs = words->getWordPtrs();
	int32_t  *wlens = words->getWordLens();

	// hash in the prefix
	uint64_t prefixHash = 0LL;
	int32_t plen = 0;
	if ( hi->m_prefix ) plen = gbstrlen ( hi->m_prefix );
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
	HashTableX ut; ut.set ( 8,0,0,NULL,0,false,niceness,"uqtbl");

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
	if ( !getDiversityVec( words, phrases, countTable, &dwbuf, niceness ) ) {
		return false;
	}
	char *wdv = dwbuf.getBufStart();

	int32_t nw = words->getNumWords();

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
	if ( ! getDensityRanks((int64_t *)wids,
			       nw,
			       hi->m_hashGroup,
			       &densBuf,
			       sections,
			       m_niceness))
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
	if ( ! getWordPosVec ( words , sections, m_dist, fragVec, niceness, &wpos) )
		return false;

	// a handy ptr
	int32_t *wposvec = (int32_t *)wpos.getBufStart();

	int32_t i;
	for ( i = 0 ; i < nw ; i++ ) {
		// breathe
		QUICKPOLL(niceness);
		if ( ! wids[i] ) continue;
		// ignore if in repeated fragment
		if ( fragVec && i<MAXFRAGWORDS && fragVec[i] == 0 ) continue;
		// ignore if in style section
		if ( sp && (sp[i]->m_flags & NOINDEXFLAGS) ) continue;

		// do not breach wordpos bits
		if ( wposvec[i] > MAXWORDPOS ) break;

		// BR: 20160114 if digit, do not hash it if disabled
		if( is_digit( wptrs[i][0] ) && !hi->m_hashNumbers ) {
			continue;
		}

		// . hash the startHash with the wordId for this word
		// . we must mask it before adding it to the table because
		//   this table is also used to hash IndexLists into that come
		//   from LinkInfo classes (incoming link text). And when
		//   those IndexLists are hashed they used masked termIds.
		//   So we should too...
		uint64_t h ;
		if ( plen > 0 ) h = hash64 ( wids[i] , prefixHash );
		else            h = wids[i];

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
		char langId = langUnknown;
		if ( m_wts && langVec ) langId = langVec[i];

		char wd;
		if ( hi->m_useCountTable ) {
			wd = wdv[i];
		} else {
			wd = MAXDIVERSITYRANK;
		}

		// BR 20160115: Don't hash 'junk' words
		bool skipword = false;
		if( !hi->m_hashCommonWebWords )
		{
			
			// Don't hash the words below as individual words.
			// Yes, hack'ish. Will have to do for now..
			switch( wlens[i] )
			{
				case 2:
					if( memcmp(wptrs[i], "uk", 2) == 0 ||
						memcmp(wptrs[i], "de", 2) == 0 ||
						memcmp(wptrs[i], "dk", 2) == 0 ||
						memcmp(wptrs[i], "co", 2) == 0 ||
						memcmp(wptrs[i], "cn", 2) == 0 ||
						memcmp(wptrs[i], "ru", 2) == 0 )
					{
						// Skip single word but include bigram (for domain searches)
						skipword = true;
					}
					break;

				case 3:
					if( memcmp(wptrs[i], "www", 3) == 0 ||
						memcmp(wptrs[i], "com", 3) == 0 ||
						memcmp(wptrs[i], "net", 3) == 0 ||
						memcmp(wptrs[i], "org", 3) == 0 ||
						memcmp(wptrs[i], "biz", 3) == 0 ||
						memcmp(wptrs[i], "edu", 3) == 0 ||
						memcmp(wptrs[i], "gov", 3) == 0 )
					{
						// Skip single word but include bigram (for domain searches)
						skipword = true;
					}
					break;

				case 4:
					if( memcmp(wptrs[i], "http", 4) == 0 )
					{
						// Never include as single word or in bigrams
						continue;
					}
					break;

				case 5:
					if( memcmp(wptrs[i], "https", 5) == 0 )
					{
						// Never include as single word or in bigrams
						continue;
					}
					break;
					
				default:
					break;
			}
			
			if( skipword )
			{
				// sticking to the gb style ;)
				goto skipsingleword;
			}
		}

		
		// if using posdb
		key144_t k;

		g_posdb.makeKey ( &k ,
				  h ,
				  0LL,//docid
				  wposvec[i], // dist,
				  densvec[i],// densityRank , // 0-15
				  wd, // diversityRank 0-15
				  ws, // wordSpamRank  0-15
				  0, // siterank
				  hashGroup ,
				  // we set to docLang final hash loop
				  langUnknown, // langid
				  0 , // multiplier
				  false , // syn?
				  false , // delkey?
				  hi->m_shardByTermId );

		// key should NEVER collide since we are always incrementing
		// the distance cursor, m_dist
		dt->addTerm144 ( &k );

		// add to wts for PageParser.cpp display
		if ( wts ) {
			if ( ! storeTerm ( wptrs[i],wlens[i],h,hi,i,
					   wposvec[i], // wordPos
					   densvec[i],// densityRank , // 0-15
					   wd,//v[i],
					   ws,
					   hashGroup,
					   wbuf,
					   wts,
					   SOURCE_NONE, // synsrc
					   langId ,
					   k))
				return false;
		}

		//
		// STRIP POSSESSIVE WORDS for indexing
		//
		// . for now do simple stripping here
		// . if word is "bob's" hash "bob"
		//

		//@todo BR 20160107: Is this always good? Is the same done in Query.cpp?
		if ( wlens[i] >= 3 &&
		     wptrs[i][wlens[i]-2] == '\'' &&
		     to_lower_a(wptrs[i][wlens[i]-1]) == 's' ) {
			int64_t nah ;
			nah = hash64Lower_utf8 ( wptrs[i], wlens[i]-2 );
			if ( plen>0 ) nah = hash64 ( nah , prefixHash );
			g_posdb.makeKey ( &k ,
					  nah,
					  0LL,//docid
					  wposvec[i], // dist,
					  densvec[i],// densityRank , // 0-15
					  wd,//v[i], // diversityRank ,
					  ws, // wordSpamRank ,
					  0, //siterank
					  hashGroup,
					  // we set to docLang final hash loop
					  langUnknown, // langid
					  0 , // multiplier
					  true  , // syn?
					  false , // delkey?
					  hi->m_shardByTermId );
			// key should NEVER collide since we are always
			// incrementing the distance cursor, m_dist
			dt->addTerm144 ( &k );
			// keep going if not debug
			if ( ! wts ) continue;
			// print the synonym
			if ( ! storeTerm(wptrs[i], // synWord,
					 wlens[i] -2, // gbstrlen(synWord),
					 nah,  // termid
					 hi,
					 i, // wordnum
					 wposvec[i], // wordPos
					 densvec[i],// densityRank , // 0-15
					 wd,//v[i],
					 ws,
					 hashGroup,
					 //false, // is phrase?
					 wbuf,
					 wts,
					 SOURCE_GENERATED,
					 langId,
					 k) )
				return false;
		}



skipsingleword:
		////////
		//
		// two-word phrase
		//
		////////

		int64_t npid = pids2[i];
		uint64_t  ph2 = 0;

		// repeat for the two word hash if different!
		if ( npid ) {
			// hash with prefix
			if ( plen > 0 ) ph2 = hash64 ( npid , prefixHash );
			else            ph2 = npid;
			g_posdb.makeKey ( &k ,
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
					  true  , // syn?
					  false , // delkey?
					  hi->m_shardByTermId );
			// key should NEVER collide since we are always
			// incrementing the distance cursor, m_dist
			dt->addTerm144 ( &k );
		}

		// add to wts for PageParser.cpp display
		if ( wts && npid ) {
			// get phrase as a string
			int32_t plen;
			char *phr= phrases->getPhrase(i, &plen);
			// store it
			if ( ! storeTerm ( phr,plen,ph2,hi,i,
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
					   k) )
				return false;
		}


		//
		// NUMERIC SORTING AND RANGES
		//

		// only store numbers in fields this way
		if ( prefixHash == 0 )
		{
			continue;
		}

		// this may or may not be numeric.
		if ( ! is_digit ( wptrs[i][0] ) )
		{
			continue;
		}

		// Avoid creating "sortby" number values in posdb if not wanted
		if( !hi->m_createSortByForNumbers )
		{
			continue;
		}

		// this might have to "back up" before any '.' or '-' symbols
		if ( ! hashNumberForSorting ( wptrs[0] ,
				    wptrs[i] ,
				    wlens[i] ,
				    hi ) )
			return false;
	}

	// between calls? i.e. hashTitle() and hashBody()
	if ( i > 0 ) m_dist = wposvec[i-1] + 100;

	return true;
}

bool XmlDoc::hashFieldMatchTerm ( char *val , int32_t vlen , HashInfo *hi ) {

	HashTableX *tt = hi->m_tt;

	uint64_t val64 = hash64 ( val , vlen );

	// term is like something like "object.price" or whatever.
	// it is the json field itself, or the meta tag name, etc.
	uint64_t middlePrefix = hash64n ( hi->m_prefix );

	// hash "This is a new product." with "object.desc".
	// "object.desc" (termId64) is case-sensitive.
	uint64_t composite = hash64 ( val64 , middlePrefix );

        // hash that with "gbfieldmatch"
	char *prefix = "gbfieldmatch";
	uint64_t prefixHash = hash64n ( prefix );
	uint64_t ph2 = hash64 ( composite , prefixHash );

	// . now store it
	// . use field hash as the termid. normally this would just be
	//   a prefix hash
	// . use mostly fake value otherwise
	key144_t k;
	g_posdb.makeKey ( &k ,
			  ph2 ,
			  0,//docid
			  0,// word pos #
			  0,// densityRank , // 0-15
			  0 , // MAXDIVERSITYRANK
			  0 , // wordSpamRank ,
			  0 , //siterank
			  0 , // hashGroup,
			  langUnknown,
			  0 , // multiplier
			  false, // syn?
			  false , // delkey?
			  false ) ; // shardByTermId? no, by docid.

	HashTableX *dt = tt;//hi->m_tt;

	// the key may indeed collide, but that's ok for this application
	if ( ! dt->addTerm144 ( &k ) )
		return false;

	if ( ! m_wts )
		return true;

	// store in buffer for display on pageparser.cpp output
	char buf[128];
	int32_t bufLen ;
	bufLen = sprintf(buf,"gbfieldmatch:%s:%"UINT64"",hi->m_prefix,val64);

	// make a special hashinfo for this facet
	HashInfo hi2;
	hi2.m_tt = tt;
	// the full prefix
	char fullPrefix[64];
	snprintf(fullPrefix,62,"%s:%s",prefix,hi->m_prefix);
	hi2.m_prefix = fullPrefix;

	// add to wts for PageParser.cpp display
	// store it
	if ( ! storeTerm ( buf,
			   bufLen,
			   ph2,
			   &hi2,
			   0, // word#, i,
			   0, // wordPos
			   0,// densityRank , // 0-15
			   0, // MAXDIVERSITYRANK,//phrase
			   0, // ws,
			   0, // hashGroup,
			   //true,
			   &m_wbuf,
			   m_wts,
			   // a hack for display in wts:
			   SOURCE_NUMBER, // SOURCE_BIGRAM, // synsrc
			   langUnknown ,
			   k) )
		return false;

	return true;
}


// . we store numbers as floats in the top 4 bytes of the lower 6 bytes of the
//   posdb key
// . the termid is the hash of the preceeding field
// . in json docs a field is like "object.details.price"
// . in meta tags it is just the meta tag name
// . credit card numbers are 16 digits. we'd need like 58 bits to store those
//   so we can't do that here, but we can approximate as a float
// . the binary representation of floating point numbers is ordered in the
//   same order as the floating points themselves! so we are lucky and can
//   keep our usually KEYCMP sorting algos to keep the floats in order.
bool XmlDoc::hashNumberForSorting ( char *beginBuf ,
			  char *buf ,
			  int32_t bufLen ,
			  HashInfo *hi ) {

	if ( ! is_digit(buf[0]) ) return true;

	char *p = buf;
	char *bufEnd = buf + bufLen;

	// back-up over any .
	if ( p > beginBuf && p[-1] == '.' ) p--;

	// negative sign?
	if ( p > beginBuf && p[-1] == '-' ) p--;

	//
	// also hash as an int, 4 byte-integer so our lastSpidered timestamps
	// dont lose 128 seconds of resolution
	//

	int32_t i = (int32_t) atoll2 ( p , bufEnd - p );

	if ( ! hashNumberForSortingAsInt32 ( i , hi , "gbsortbyint" ) )
		return false;

	// also hash in reverse order for sorting from low to high
	i = -1 * i;

	if ( ! hashNumberForSortingAsInt32 ( i , hi , "gbrevsortbyint" ) )
		return false;


	return true;
}

bool XmlDoc::hashNumberForSortingAsInt32 ( int32_t n , HashInfo *hi , char *sortByStr ) {

	// prefix is something like price. like the meta "name" or
	// the json name with dots in it like "product.info.price" or something
	int64_t nameHash = 0LL;
	int32_t nameLen = 0;
	if ( hi->m_prefix ) nameLen = gbstrlen ( hi->m_prefix );
	if ( hi->m_prefix && nameLen )
		nameHash = hash64Lower_utf8_nospaces( hi->m_prefix , nameLen );
	// need a prefix for hashing numbers... for now
	else { char *xx=NULL; *xx=0; }

	// combine prefix hash with a special hash to make it unique to avoid
	// collisions. this is the "TRUE" prefix.
	int64_t truePrefix64 = hash64n ( sortByStr ); // "gbsortby");
	// hash with the "TRUE" prefix
	int64_t ph2 = hash64 ( nameHash , truePrefix64 );

	// . now store it
	// . use field hash as the termid. normally this would just be
	//   a prefix hash
	// . use mostly fake value otherwise
	key144_t k;
	g_posdb.makeKey ( &k ,
			  ph2 ,
			  0,//docid
			  0,// word pos #
			  0,// densityRank , // 0-15
			  0 , // MAXDIVERSITYRANK
			  0 , // wordSpamRank ,
			  0 , //siterank
			  0 , // hashGroup,
			  // we set to docLang final hash loop
			  //langUnknown, // langid
			  // unless already set. so set to english here
			  // so it will not be set to something else
			  // otherwise our floats would be ordered by langid!
			  // somehow we have to indicate that this is a float
			  // termlist so it will not be mangled any more.
			  //langEnglish,
			  langUnknown,
			  0 , // multiplier
			  false, // syn?
			  false , // delkey?
			  hi->m_shardByTermId );

	//int64_t final = hash64n("products.offerprice",0);
	//int64_t prefix = hash64n("gbsortby",0);
	//int64_t h64 = hash64 ( final , prefix);
	//if ( ph2 == h64 )
	//	log("hey: got offer price");
	// now set the float in that key
	//g_posdb.setFloat ( &k , f );
	g_posdb.setInt ( &k , n );

	// HACK: this bit is ALWAYS set by Posdb::makeKey() to 1
	// so that we can b-step into a posdb list and make sure
	// we are aligned on a 6 byte or 12 byte key, since they come
	// in both sizes. but for this, hack it off to tell
	// addTable144() that we are a special posdb key, a "numeric"
	// key that has a float stored in it. then it will NOT
	// set the siterank and langid bits which throw our sorting
	// off!!
	g_posdb.setAlignmentBit ( &k , 0 );

	// sanity
	//float t = g_posdb.getFloat ( &k );
	int32_t x = g_posdb.getInt ( &k );
	if ( x != n ) { char *xx=NULL;*xx=0; }

	HashTableX *dt = hi->m_tt;

	// the key may indeed collide, but that's ok for this application
	if ( ! dt->addTerm144 ( &k ) )
		return false;

	if ( ! m_wts )
		return true;

	// store in buffer
	char buf[128];
	snprintf(buf,126,"%s:%s int32=%"INT32"",sortByStr, hi->m_prefix,n);
	int32_t bufLen = gbstrlen(buf);

	// add to wts for PageParser.cpp display
	// store it
	if ( ! storeTerm ( buf,
			   bufLen,
				ph2,
			   hi,
			   0, // word#, i,
			   0, // wordPos
			   0,// densityRank , // 0-15
			   0, // MAXDIVERSITYRANK,//phrase
			   0, // ws,
			   0, // hashGroup,
			   //true,
			   &m_wbuf,
			   m_wts,
			   // a hack for display in wts:
			   SOURCE_NUMBER, // SOURCE_BIGRAM, // synsrc
			   langUnknown ,
			   k ) )
		return false;

	return true;
}




// . returns -1 if blocked, returns NULL and sets g_errno on error
// . hash each json VALUE (not FIELD) ... AND ... hash each json
//   VALUE with its FIELD like "title:cool" or "description:whatever"
// . example:
//   [{"id":"b7df5d33-3fe5-4a6c-8ad4-dad495b586cd","finish":1378322570280,"matched":64,"status":"Stopped","start":1378322184332,"token":"poo","parameterMap":{"token":"poo","seed":"www.alleyinsider.com","api":"article"},"crawled":64},{"id":"830e0584-7f69-4bdd-

#include "Json.h"

char *XmlDoc::hashJSONFields2 ( HashTableX *table ,
				HashInfo *hi , Json *jp ,
				bool hashWithoutFieldNames ) {

	JsonItem *ji = jp->getFirstItem();

	char nb[1024];
	SafeBuf nameBuf(nb,1024);

	//int32_t totalHash32 = 0;

	for ( ; ji ; ji = ji->m_next ) {
		QUICKPOLL(m_niceness);
		// skip if not number or string
		if ( ji->m_type != JT_NUMBER && ji->m_type != JT_STRING )
			continue;
		// reset, but don't free mem etc. just set m_length to 0
		nameBuf.reset();

		// get its full compound name like "meta.twitter.title"
		JsonItem *p = ji;
		char *lastName = NULL;
		char *nameArray[20];
		int32_t  numNames = 0;
		for ( ; p ; p = p->m_parent ) {
			// empty name?
			if ( ! p->m_name ) continue;
			if ( ! p->m_name[0] ) continue;
			// dup? can happen with arrays. parent of string
			// in object, has same name as his parent, the
			// name of the array. "dupname":[{"a":"b"},{"c":"d"}]
			if ( p->m_name == lastName ) continue;
			// update
			lastName = p->m_name;
			// add it up
			nameArray[numNames++] = p->m_name;
			// breach?
			if ( numNames < 15 ) continue;
			log("build: too many names in json tag");
			break;
		}

		// if we are the diffbot reply "html" field do not hash this
		// because it is redundant and it hashes html tags etc.!
		// plus it slows us down a lot and bloats the index.
		if ( ji->m_name && numNames==1 && strcmp(ji->m_name,"html")==0)
			continue;

		// assemble the names in reverse order which is correct order
		for ( int32_t i = 1 ; i <= numNames ; i++ ) {
			// copy into our safebuf
			if ( ! nameBuf.safeStrcpy ( nameArray[numNames-i]) )
				return NULL;
			// separate names with periods
			if ( ! nameBuf.pushChar('.') ) return NULL;
		}
		// remove last period
		nameBuf.removeLastChar('.');
		// and null terminate
		if ( ! nameBuf.nullTerm() ) return NULL;
		// change all :'s in names to .'s since : is reserved!
		char *px = nameBuf.getBufStart();
		for ( ; *px ; px++ ) if ( *px == ':' ) *px = '.';
		//for ( px = nameBuf.getBufStart(); *px ; px++ ) if ( *px == '-' ) *px = '_';
		//
		// DIFFBOT special field hacks
		//
		char *name = nameBuf.getBufStart();
		hi->m_hashGroup = HASHGROUP_BODY;
		if ( strstr(name,"title") )
			hi->m_hashGroup = HASHGROUP_TITLE;
		if ( strstr(name,"url") )
			hi->m_hashGroup = HASHGROUP_INURL;
		if ( strstr(name,"resolved_url") )
			hi->m_hashGroup = HASHGROUP_INURL;
		if ( strstr(name,"tags") )
			hi->m_hashGroup = HASHGROUP_INTAG;
		if ( strstr(name,"meta") )
			hi->m_hashGroup = HASHGROUP_INMETATAG;
		//
		// now Json.cpp decodes and stores the value into
		// a buffer, so ji->getValue() should be decoded completely
		//

		// . get the value of the json field
		// . if it's a number or bool it converts into a string
		int32_t vlen;
		char *val = ji->getValueAsString( &vlen );
		char tbuf[32];

		// if the value is clearly a date, just hash it as
		// a number, so use a temporary value that holds the
		// time_t and hash with that... this will hash
		// diffbot's article date field as a number so we can
		// sortby and constrain by it in the search results
		if ( name && (strcasecmp(name,"date") == 0 || strcasecmp(name,"estimatedDate") == 0)) {
			// this is in HttpMime.cpp
			int64_t tt = atotime1 ( val );
			// we can't store 64-bit dates... so truncate to -2147483648
			// which is Dec 13 1901. so we don't quite get the 1898 date
			// for the new york times dbpedia entry. maybe if we added
			// an extra termlist for more precision to indicate century or
			// something.
			if ( tt && tt < (int32_t)0x80000000 )
				tt = (int32_t)0x80000000;
			// likewise, we can't be too big, passed 2038
			if ( tt && tt > 0x7fffffff )
				tt = (int32_t)0x7fffffff;
			if ( tt ) {
				// print out the time_t in ascii
				vlen = sprintf(tbuf,"%"INT32"",(int32_t)tt);
				// and point to it for hashing/indexing
				val = tbuf;
			}
		}

		// index like "title:whatever"
		hi->m_prefix = name;
		hashString ( val , vlen , hi );

		//log("hashing json var as %s %s %d", name, val, vlen);

		// hash gbfieldmatch:some.fieldInJson:"case-sens field Value"
		if ( name )
			hashFieldMatchTerm ( val , (int32_t)vlen , hi );

		if ( ! hashWithoutFieldNames )
			continue;

		// hash without the field name as well
		hi->m_prefix = NULL;
		hashString ( val , vlen , hi );

	}

	return (char *)0x01;
}
