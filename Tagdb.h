// Matt Wells, copyright Jul 2008

#ifndef GB_TAGDB_H
#define GB_TAGDB_H

#include <set>
#include "Rdb.h"
#include "Xml.h"
#include "Url.h"
#include "Loop.h"
#include "SafeBuf.h"
#include "Msg0.h"
#include "GbMutex.h"

// . Tag::m_type is this if its a dup in the TagRec
// . so if www.xyz.com has one tag and xyz.com has another, then
//   the xyz.com tag should have its m_type set to TT_DUP, but only if we can only have
//   one of those tag types...
#define TT_DUP 123456

#define TAGDB_KEY key128_t

class HttpRequest;
class TcpSocket;

// a TagRec can contain multiple Tags, even of the same Tag::m_type
class Tag {
 public:

	int32_t  getRecSize ( ) { return sizeof(key128_t) + 4 + m_recDataSize; }

	void set ( const char *site, const char *tagname, int32_t  timestamp, const char *user,
	           int32_t ip, const char *data, int32_t  dataSize );

	bool printToBuf             ( SafeBuf *sb );
	bool printToBufAsAddRequest ( SafeBuf *sb );
	bool printToBufAsXml        ( SafeBuf *sb );
	bool printToBufAsHtml       ( SafeBuf *sb , const char *prefix );
	bool printToBufAsTagVector  ( SafeBuf *sb );
	// just print the m_data...
	bool printDataToBuf         ( SafeBuf *sb );
	bool isType                 ( const char *t );

	// for parsing output of printToBuf()
	int32_t setFromBuf            ( char *p , char *pend ) ;
	int32_t setDataFromBuf        ( char *p , char *pend ) ;

	// skip of the username, whose size (including \0) is encoded
	// as the first byte in the m_recData buffer
	char *getTagData     ( ) {return m_buf + *m_buf + 1;}
	int32_t  getTagDataSize ( ) {return m_bufSize - *m_buf - 1; }

	// what user added this tag?
	char *getUser ( ) { return m_buf + 1;}

	// remove the terminating \0 which is included as part of the size
	int32_t  getUserLen ( ) { return *m_buf - 1; }

	// used to determine if one Tag should overwrite the other! if they
	// have the same dedup hash... then yes...
	int32_t getDedupHash ( );

	// tagdb uses 128 bit keys now
	key128_t  m_key;
	int32_t      m_recDataSize;

	// when tag was added/updated
	int32_t     m_timestamp;

	// . ip address of user adding tag
	// . prevent multiple turk voters from same ip!
	int32_t     m_ip;        

	// the "type" of tag. see the TagDesc array in Tagdb.cpp for a list
	// of all the tag types. m_type is a hash of the type name.
	int32_t     m_type;

	int32_t     m_bufSize;
	char     m_buf[0];
} __attribute__((packed, aligned(4)));

// . convert "domain_squatter" to ST_DOMAIN_SQUATTER
// . used by CollectionRec::getRegExpNum()
int32_t  getTagTypeFromStr( const char *tagTypeName , int32_t tagnameLen = -1 );

// . convert ST_DOMAIN_SQUATTER to "domain_squatter"
const char *getTagStrFromType ( int32_t tagType ) ;

std::set<int64_t> getDeprecatedTagTypes();

// max "oustanding" msg0 requests sent by TagRec::lookup()
#define MAX_TAGDB_REQUESTS 3

class TagRec {
public:
	TagRec();
	~TagRec();
	void reset();
	void constructor ();

	int32_t getNumTags ( );

	Tag *getFirstTag ( );

	// lists should be in order of precedence i guess
	Tag *getNextTag ( Tag *tag );

	// return the number the tags having particular tag types
	int32_t getNumTagTypes ( const char *tagTypeStr );

	// get a tag from the tagType
	Tag *getTag ( const char *tagTypeStr );
	Tag *getTag2 ( int32_t tagType );

	// . for showing under the summary of a search result in PageResults
	// . also for Msg6a
	int32_t print ( ) ; 
	bool printToBuf             ( SafeBuf *sb );
	bool printToBufAsAddRequest ( SafeBuf *sb );
	bool printToBufAsXml        ( SafeBuf *sb );
	bool printToBufAsHtml       ( SafeBuf *sb , const char *prefix );
	bool printToBufAsTagVector  ( SafeBuf *sb );

	// . functions to act on a site "tag buf", like that in Msg16::m_tagRec
	// . first 2 bytes is size, 2nd to bytes is # of tags, then the tags
	int32_t getLong ( const char *tagTypeStr, int32_t defalt,
	                  int32_t *timeStamp = NULL, const char **user = NULL );

	int64_t getLongLong ( const char *tagTypeStr, int64_t defalt,
	                      int32_t *timeStamp = NULL , const char **user = NULL );

	const char *getString ( const char *tagTypeStr, const char *defalt = NULL, int32_t *size = NULL,
	                        int32_t *timestamp = NULL, const char **user = NULL );

	bool setFromBuf ( char *buf , int32_t bufSize );
	bool serialize ( SafeBuf &dst );

	bool setFromHttpRequest ( HttpRequest *r , TcpSocket *s );

	// use this for setFromBuf()
	SafeBuf m_sbuf;
	
	// hold possible tagdb records
	RdbList m_lists[MAX_TAGDB_REQUESTS];

	// ptrs to lists in the m_lists[] array
	RdbList *m_listPtrs[MAX_TAGDB_REQUESTS];
	int32_t     m_numListPtrs;
};

class Tagdb  {

 public:
	// reset rdb
	void reset();

	// . TODO: specialized cache because to store pre-parsed tagdb recs
	// . TODO: have m_useSeals parameter???
	bool init  ( );

	// used by ../rdb/Msg0 and ../rdb/Msg1
	Rdb *getRdb ( ) { return &m_rdb; }

	static key128_t makeStartKey(const char *site);
	static key128_t makeStartKey(const char *site, int32_t siteLen);

	static key128_t makeEndKey(const char *site);
	static key128_t makeEndKey(const char *site, int32_t siteLen);

	static key128_t makeDomainStartKey(Url *u);
	static key128_t makeDomainEndKey(Url *u);

	// private:

	void setHashTable ( ) ;

	// . we use the cache in here also for caching tagdb records
	//   and "not-founds" stored remotely (net cache)
	Rdb   m_rdb;

	bool    loadMinSiteInlinksBuffer ( );
	bool    loadMinSiteInlinksBuffer2 ( );
	int32_t getMinSiteInlinks ( uint32_t hostHash32 ) ;
	SafeBuf m_siteBuf1;
	SafeBuf m_siteBuf2;

	static Tag *addTag (SafeBuf *sb, const char *mysite, const char *tagname, int32_t now, const char *user, int32_t ip,
			    const  char *data, int32_t dsize, rdbid_t rdbId, bool pushRdbId);
	static Tag *addTag2(SafeBuf *sb, const char *mysite,const  char *tagname, int32_t now, const char *user, int32_t ip,
			    int32_t val, rdbid_t rdbId);
	static Tag *addTag3(SafeBuf *sb, const char *mysite, const char *tagname, int32_t now, const char *user, int32_t ip,
			    const char *data, rdbid_t rdbId);
	static bool addTag (SafeBuf *sb, Tag *tag);
};

extern class Tagdb  g_tagdb;
extern class Tagdb  g_tagdb2;

bool sendPageTagdb ( TcpSocket *s , HttpRequest *req ) ;



///////////////////////////////////////////////
//
// Msg8a gets TagRecs from Tagdb
//
///////////////////////////////////////////////

class Msge0;


// this msg class is for getting AND adding to tagdb
class Msg8a {

 public:
	
	Msg8a ();
	~Msg8a ();
	void reset();

	// . get records from multiple subdomains of url
	// . calls g_udpServer.sendRequest() on each subdomain of url
	// . all matching records are merge into a final record
	//   i.e. site tags are also propagated accordingly
	// . closest matching "site" is used as the "site" (the site url)
	// . stores the tagRec in your "tagRec"
	bool getTagRec( Url *url, collnum_t collnum, int32_t niceness, void *state, void (*callback)( void * ),
	                TagRec *tagRec );
	
private:
	bool launchGetRequests();
	void gotAllReplies ( ) ;

	static void gotMsg0ReplyWrapper(void *);

	// some specified input
	Url   *m_url;

	collnum_t m_collnum;

	void    (*m_callback ) ( void *state );
	void     *m_state;

	Msg0    m_msg0s[MAX_TAGDB_REQUESTS];

	key128_t m_siteStartKey ;
	key128_t m_siteEndKey   ;

	int32_t m_niceness;

	const char *m_dom;
	char *m_hostEnd;
	char *m_p;

	int32_t  m_requests;
	int32_t  m_replies;
	bool  m_doneLaunching;
	GbMutex m_mtx;

	int32_t  m_errno;

	// we set this for the caller
	TagRec *m_tagRec;

public:
	// hack for MsgE0
	Msge0 *m_msge0;
	int32_t m_msge0State;
};

#endif // GB_TAGDB_H

// Lookup order for the url hostname.domainname.com/mydir/mypage.html 
// (aka 192.0.2.4/mydir/mypage.html):

//  . hostname.domainname.com/mydir/mypage.html
//  . hostname.domainname.com/mydir/
//  . hostname.domainname.com
//  .          domainname.com/mydir/mypage.html
//  .          domainname.com/mydir/
//  .          domainname.com
//  .          192.0.2.4       /mydir/mypage.html
//  .          192.0.2.4       /mydir/
//  .          192.0.2.4                
//  .          192.0.2
