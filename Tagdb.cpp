#include "gb-include.h"

#include <sys/stat.h>
#include "Titledb.h"
#include "Tagdb.h"
#include "Unicode.h"
#include "JobScheduler.h"
#include "Msg1.h"
#include "HttpServer.h"
#include "Pages.h"
#include "SiteGetter.h"
#include "HashTableX.h"
#include "Process.h"
#include "Rebalance.h"
#include "RdbCache.h"
#include "GbMutex.h"
#include "ScopedLock.h"

static void gotMsg0ReplyWrapper ( void *state );

static HashTableX s_ht;
static bool s_initialized = false;
static GbMutex s_htMutex;


// to stdout
int32_t Tag::print ( ) {
	SafeBuf sb;
	printToBuf ( &sb );

	// dump that
	return fprintf(stderr,"%s\n",sb.getBufStart());
}

bool Tag::printToBuf ( SafeBuf *sb ) {
	sb->safePrintf("k.hsthash=%016" PRIx64" k.duphash=%08" PRIx32" k.sitehash=%08" PRIx32" ",
	               m_key.n1, (int32_t)(m_key.n0>>32), (int32_t)(m_key.n0&0xffffffff));

	// print the tagname
	sb->safePrintf ( "TAG=%s,\"%s\",", getTagStrFromType(m_type), getUser() );

	// print the date when this tag was added
	time_t ts = m_timestamp;
	struct tm tm_buf;
	struct tm *timeStruct = localtime_r(&ts,&tm_buf);
	char tmp[100];
	strftime(tmp,100,"%b-%d-%Y-%H:%M:%S,",timeStruct);
	sb->safePrintf("%s(%" PRIu32"),",tmp,m_timestamp);

	// print the ip added from
	sb->safePrintf("%s,",iptoa(m_ip));

	sb->safePrintf("\"");
	if ( ! printDataToBuf ( sb ) ) {
		return false;
	}

	// final quote
	sb->safePrintf("\"");
	return true;
}

// . "site" can also be a specific url, but it must be normalized
// . i.e. of the form http://xyz.com/
void Tag::set ( const char *site, const char *tagname, int32_t  timestamp, const char *user, int32_t  ip,
                const char *data, int32_t  dataSize ) {
	// get type from name
	m_type = getTagTypeFromStr ( tagname , strlen(tagname) );

	m_timestamp = timestamp;
	m_ip        = ip;
	int32_t userLen = 0;
	if ( user ) {
		userLen = strlen( user );
	}

	// truncate to 127 byte int32_t
	if ( userLen > 126 ) {
		userLen = 126;
	}

	// normalize
	Url norm;
	norm.set ( site );

	char *p = m_buf;

	// store size (includes \0)
	*p++ = userLen + 1;

	// then user name
	gbmemcpy ( p , user , userLen );
	p += userLen;

	// then \0
	*p++ = '\0';

	// store data now too
	gbmemcpy ( p , data , dataSize );
	p += dataSize;

	// NULL terminate if they did not! now all tag are strings and must
	// be NULL terminated.
	if ( data && p[-1] ) {
		*p++ = '\0';
	}

	// set it
	m_bufSize = p - m_buf;

	// i had to make this the hash of the site, not host, 
	// because www.last.fm/user/xxxxx/
	// was making the rdblist a few megabytes big!!
	m_key.n1 = hash64n ( site );

	// assume we are unique tag, that many of this type can exist
	uint32_t upper32 = getDedupHash(); // m_type;

	// put in upper 32
	m_key.n0 = upper32;
	// shift it up
	m_key.n0 <<= 32;

	// set positive bit so its not a delete record
	m_key.n0 |= 0x01;

	// the size of this class as an Rdb record
	m_recDataSize = m_bufSize + sizeof(Tag) - sizeof(key128_t) - 4;
}

// . return # of ascii chars scanned in "p"
// . return 0 on error
// . parses output of printToBuf() above
// . k.n1=0x695b3 k.n0=0xa4118684fa4edf93 version=0 TAG=ruleset,"mwells",Jan-02-2009-18:26:04,<timestamp>,67.16.94.2,3735437892,36 TAG=blog,"mwells",Jan-02-2009-18:26:04,67.16.94.2,2207516434,1 TAG=site,"tagdb",Jan-02-2009-18:26:04,0.0.0.0,833534375,mini-j-gaidin.livejournal.com/
int32_t Tag::setFromBuf ( char *p , char *pend ) {
	// save our place
	char *start = p;

	// tags always start with " TAG="
	if ( strncmp(p," TAG=",5) ) {
		log("tagdb: error processing tag in setFromBuf().");
		return 0;
	}

	// skip that
	p += 5;

	// get the type
	char *type = p;

	// get type length
	while ( p < pend && *p != ',' ) p++;

	// error?
	if ( p == pend ) return 0;

	// that is the length
	int32_t typeLen = p - type;

	// convert to number
	m_type = getTagTypeFromStr ( type , typeLen );

	// panic?
	if ( m_type == -1 ) { g_process.shutdownAbort(true);}

	// now the user, skip comma and quote
	p+=2;

	// data buffer
	char *dst = m_buf;

	// point to it
	char *user = p;

	// get end of it
	while ( p < pend && *p != '\"' ) p++;

	// error?
	if ( p == pend ) return 0;

	// set length
	int32_t userLen = p - user;

	// sanity. username total buf space including \0 <= 8
	if ( userLen > 126 ) userLen = 126;

	// first byte is username size
	*dst++ = userLen+1;

	// then the username
	gbmemcpy ( dst , user , userLen );
	dst += userLen;

	// and finall null termination
	*dst++ = '\0';

	// skip quote and comma
	p+=2;

	// that is the time stamp in canonical form
	// skip till comma
	while ( p < pend && *p != ',' ) p++;

	// error?
	if ( p == pend ) return 0;

	// skip comma
	p++;

	// save start
	char *ts = p;

	// skip until comma again
	while ( p < pend && *p != ',' ) p++;

	// error?
	if ( p == pend ) return 0;

	// this is the timestamp in seconds since epoch
	m_timestamp = atoi(ts);

	// skip comma
	p++;

	// ip address as text
	char *ips = p;

	// skip until comma again
	while ( p < pend && *p != ',' ) p++;

	// error?
	if ( p == pend ) return 0;

	// convert it to binary
	m_ip = atoip ( ips , p - ips );

	// skip comma
	p++;

	// . now is the data
	// . return # of chars scanned in "p"
	p += setDataFromBuf ( p , pend );

	// . sanity check
	// . all tags must be NULL terminated now
	if ( m_buf[m_bufSize-1] != '\0' ) {g_process.shutdownAbort(true); }

	// return how many bytes we read
	return p - start;
}

// . return # of chars scanned in "p"
// . return 0 on error
int32_t Tag::setDataFromBuf ( char *p , char *pend ) {
	// skip over username in the buffer to point to where to put tag data
	char *dst = m_buf + *m_buf + 1;

	// stop at space of 
	gbmemcpy(dst,p,pend-p);

	// advance
	dst += (pend-p);

	// update
	m_bufSize = dst - m_buf;

	// should be end delimter
	char c = m_buf[m_bufSize-1];

	// sanity check
	if ( c && ! isspace(c) ) { g_process.shutdownAbort(true); }

	// strings are always NULL terminated, the datasize should
	// include the NULL termination
	m_buf[m_bufSize-1]='\0';

	// we basically insert the \0, and *p should point to the space
	// right after the string...! so return m_dataSize - 1
	return m_bufSize - 1;
}

bool Tag::printDataToBuf ( SafeBuf *sb ) {
	char *data     = getTagData();
	int32_t  dataSize = getTagDataSize();

	for ( int32_t i = 0 ; data[i] && i < dataSize ; i++ )
		sb->safePrintf ( "%c" , data[i] );
	return true;
}

// /admin/tagdb?c=mdw&u=www.mdw123.com&ufu=&username=admin&tagtype0=sitenuminlinks&tagdata0=10&tagtype1=rootlang&tagdata1=&tagtype2=rootlang&tagdata2=&add=Add+Tags
bool Tag::printToBufAsAddRequest ( SafeBuf *sb ) {
	// print the tagname
	const char *str = getTagStrFromType ( m_type );
	sb->safePrintf("/admin/tagdb?");

	// print key of the tag as 16 byte key in ascii hex notation
	// we don't know the "site" for all tags because "site" is a tag
	// itself. we should take this in lieu of the "u=" url parm
	// which is made to generate the key anyhow.
	sb->safePrintf("&tagn0keyb0=%" PRId64,m_key.n0);
	sb->safePrintf("&tagn1keyb0=%" PRId64,m_key.n1);

	// print the user that added this tag
	sb->safePrintf ( "&username=%s" , getUser() );

	// the tag type, like "sitenuminlinks" or "rootlang"
	sb->safePrintf("&tagtype0=%s",str);

	// the "score"
	sb->safePrintf("&tagdata0=");

	// print the m_data
	SafeBuf tmp;
	if ( ! printDataToBuf ( &tmp ) ) {
		return false;
	}
	tmp.nullTerm();

	sb->urlEncode(tmp.getBufStart());
	sb->nullTerm();

	return true;
}

bool Tag::printToBufAsXml ( SafeBuf *sb ) {
	// print the tagname
	const char *str = getTagStrFromType ( m_type );

	// print the user that added this tag
	sb->safePrintf ("\t\t<tag>\n\t\t\t<name>%s</name>\n\t\t\t<user>%s", str,getUser());

	// print the date when this tag was added
	sb->safePrintf("</user>\n\t\t\t<timestamp>%" PRId32"</timestamp>\n", m_timestamp);

	// print the ip added from
	sb->safePrintf("\t\t\t<ip>%s</ip>\n",iptoa(m_ip));

	// the "score"
	sb->safePrintf("\t\t\t<score>");

	// print the m_data
	if ( ! printDataToBuf ( sb ) ) return false;

	sb->safePrintf("</score>\n\t\t</tag>");

	return true;
}

bool Tag::printToBufAsHtml ( SafeBuf *sb , const char *prefix ) {
	// print the tagname
	const char *str = getTagStrFromType ( m_type );

	// print the user that added this tag
	sb->safePrintf ("<tr><td>%s</td><td><b>%s</b>", prefix, str);

	// the "score"
	sb->safePrintf(" value=<b>");

	// print the m_data
	if ( ! printDataToBuf ( sb ) ) return false;

	// print the date when this tag was added
	sb->safePrintf("</b> user=%s time=",getUser());

	time_t ts = m_timestamp;
	struct tm tm_buf;
	struct tm *timeStruct = localtime_r(&ts,&tm_buf);
	char tmp[100];
	strftime(tmp,100,"%b-%d-%Y-%H:%M:%S",timeStruct);
	sb->safePrintf("%s(%" PRIu32")",tmp,m_timestamp);

	// print the ip added from
	sb->safePrintf(" ip=%s",iptoa(m_ip));
	sb->safePrintf("</td></tr>\n");

	return true;
}

bool Tag::printToBufAsTagVector ( SafeBuf *sb ) {
	// print the tagname
	const char *str = getTagStrFromType ( m_type );
	sb->safePrintf("%s:",str);

	// print the m_data
	if ( ! printDataToBuf ( sb ) ) return false;

	sb->safePrintf(" ");

	return true;
}

bool Tag::isType ( const char *t ) {
	int32_t h = hash32n ( t );
	return (m_type == h);
}

TagRec::TagRec ( ) {
	m_numListPtrs = 0;
}

void TagRec::constructor ( ) {
	m_numListPtrs = 0;

	// run a constructor on the lists
	for ( int32_t i = 0 ; i < MAX_TAGDB_REQUESTS ; ++i ) {
		m_lists[i].constructor();
	}
}

TagRec::~TagRec ( ) {
	reset();
}

void TagRec::reset ( ) {
	m_numListPtrs = 0;

	for ( int32_t i = 0 ; i < MAX_TAGDB_REQUESTS ; ++i ) {
		m_lists[i].freeList();
	}
}

Tag* TagRec::getFirstTag ( ) {
	if ( m_numListPtrs == 0 ) {
		return NULL;
	}

	return (Tag *)m_listPtrs[0]->m_list;
}

Tag* TagRec::getNextTag ( Tag *tag ) {
	// watch out
	if ( ! tag ) return NULL;

	// get rec size
	int32_t recSize = tag->getRecSize();

	// point to current tag
	char *current = (char *)tag;

	// find what list we are in
	int32_t i;
	for ( i = 0 ; i < m_numListPtrs ; i++ ) {
		if ( current <  m_listPtrs[i]->m_list    ) continue;
		if ( current >= m_listPtrs[i]->m_listEnd ) continue;
		break;
	}

	// sanity
	if ( i >= m_numListPtrs ) { g_process.shutdownAbort(true); }

	// advance
	current += recSize;

	// sanity check
	if ( recSize > 500000 || recSize < 12 ) {
		log("tagdb: corrupt tag recsize %i",(int)recSize);
		return NULL;
	}

	// breach list?
	if ( current < m_listPtrs[i]->m_listEnd) {
		return (Tag *)current;
	}

	// advance list
	i++;

	// breach of lists?
	if ( i >= m_numListPtrs ) {
		return NULL;
	}

	// return that list record then
	return (Tag *)(m_listPtrs[i]->m_list);
}

Tag *TagRec::getTag ( const char *tagTypeStr ) {
	int32_t tagType = getTagTypeFromStr ( tagTypeStr );
	return getTag2 ( tagType );
}

Tag *TagRec::getTag2 ( int32_t tagType ) {
	Tag *tag = getFirstTag();
	// loop over all tags in the buf
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		// skip if not a match
		if ( tag->m_type != tagType ) continue;
		// skip dups
		if ( tag->m_type == TT_DUP ) continue;
		// got it
		return tag;
	}
	// if not found return NULL
	return NULL;
}

// . functions to act on a site "tag buf", like that in Msg16::m_tagRec
// . first 2 bytes is size, 2nd to bytes is # of tags, then the tags
int32_t TagRec::getLong ( const char *tagTypeStr, int32_t defalt, int32_t *timestamp, const char **user ) {
	int32_t tagType = getTagTypeFromStr ( tagTypeStr );

	// start here
	Tag *tag = getFirstTag();

	// loop over all tags in the buf
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		// skip if not a match
		if ( tag->m_type != tagType ) continue;

		// skip dups
		if ( tag->m_type == TT_DUP ) continue;

		// get the value as a int32_t
		int32_t score = 0;

		// the size
		char *data     = tag->getTagData();
		int32_t  dataSize = tag->getTagDataSize();

		// if ends in NULL trunc it
		if ( data[dataSize-1] == '\0' ) dataSize--;

		// convert string to value, MUST be signed!!! the data
		// should include a \0
		score = atol2(data,dataSize);

		// timestamp, et al
		if ( timestamp ) *timestamp = tag->m_timestamp;
		if ( user      ) *user      = tag->getUser();
		return score;
	}
	// not found
	return defalt;
}

int64_t TagRec::getLongLong ( const char *tagTypeStr, int64_t defalt, int32_t *timestamp, const char **user ) {
	int32_t tagType = getTagTypeFromStr ( tagTypeStr );

	// start here
	Tag *tag = getFirstTag();

	// loop over all tags in the buf
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		// skip if not a match
		if ( tag->m_type != tagType ) continue;

		// skip dups
		if ( tag->m_type == TT_DUP ) continue;

		// get the value as a int32_t
		int64_t score = 0;

		// the size
		char *data     = tag->getTagData();
		int32_t  dataSize = tag->getTagDataSize();

		// if ends in NULL trunc it
		if ( data[dataSize-1] == '\0' ) dataSize--;

		// now everything is a string
		score = atoll2(data,dataSize);

		// timestamp, et al
		if ( timestamp ) *timestamp = tag->m_timestamp;
		if ( user      ) *user      = tag->getUser();
		return score;
	}
	// not found
	return defalt;
}

const char *TagRec::getString ( const char *tagTypeStr, const char *defalt, int32_t *size,
                                int32_t *timestamp, const char **user ) {
	int32_t tagType = getTagTypeFromStr ( tagTypeStr );

	// start here
	Tag *tag = getFirstTag();

	// loop over all tags in the buf
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		// skip if not a match
		if ( tag->m_type != tagType ) continue;

		// skip dups
		if ( tag->m_type == TT_DUP ) continue;

		// want size? includes \0 probably
		if ( size      ) *size = tag->getTagDataSize();

		// timestamp, et al
		if ( timestamp ) *timestamp = tag->m_timestamp;
		if ( user      ) *user      = tag->getUser();

		// return it
		return tag->getTagData();
	}
	// not found
	return defalt;
}

// return the number of tags having the particular TagType
int32_t TagRec::getNumTagTypes ( const char *tagTypeStr ) {
	int32_t tagType = getTagTypeFromStr ( tagTypeStr );
	int32_t numTagType = 0;
	// start at the first tag
	Tag *tag = getFirstTag();
	// loop over all tags in the buf, see if we got a dup
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		// skip dups
		if ( tag->m_type == TT_DUP ) continue;
		// if there is tagType match then increment the count
		if ( tag->m_type == tagType ) numTagType++;
	}
	return numTagType;
}

int32_t TagRec::getNumTags ( ) {
	int32_t numTags = 0;

	// start at the first tag
	Tag *tag = getFirstTag();

	// loop over all tags in the buf, see if we got a dup
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		// skip dups
		if (tag->m_type != TT_DUP) {
			numTags++;
		}
	}

	return numTags;
}

// . &tagtype%" PRId32"=<tagtype>
// . &tagdata%" PRId32"=<data>
// . &deltag%" PRId32"=1 (to delete it)
// . set &user=mwells, etc. in cookie of HttpReqest, "r" for user
// . "this" TagRec's user, ip and timestamp will be carried over to "newtr"
// . returns false and sets g_errno on error
bool TagRec::setFromHttpRequest ( HttpRequest *r, TcpSocket *s ) {
	// get the user ip address
	int32_t ip = 0;
	if ( s ) ip = s->m_ip;
	// get the time stamp
	int32_t now = getTimeGlobal();

	// . loop over all urls/sites in text area
	// . no! just use single url for now

	// put all urls in this buffer
	SafeBuf fou;

	// try from textarea if the ST_SITE was not in the tag section
	int32_t  uslen;
	const char *us = r->getString("u",&uslen);
	if ( uslen <= 0 ) us = NULL;
	if ( us ) fou.safeMemcpy ( us , uslen );

	// read in file, file of urls
	int32_t ufuLen;
	const char *ufu = r->getString("ufu",&ufuLen);
	if ( ufuLen <= 0 ) ufu = NULL;
	if ( us  ) ufu = NULL; // exclusive
	if ( ufu ) fou.fillFromFile ( ufu );

	// if st->m_urls has multiple urls, this "u" is not given in the
	// http request! but a filename is... and Msg9::addTags() should add
	// the ST_SITE field anyway...
	if ( ! ufu && ! us ) return true;

	// make it null terminated since we no longer do this automatically
	fou.pushChar('\0');

	// loop over all tags in the TagRec to mod them
	for ( int32_t i = 0 ; ; ++i ) {
		char buf[32];
		sprintf ( buf , "tagtype%" PRId32,i );
		const char *tagTypeStr = r->getString(buf,NULL,NULL);
		// if not there we are done
		if ( ! tagTypeStr ) break;

		// should we delete it?
		sprintf ( buf , "deltag%" PRId32,i);
		const char *deltag = r->getString(buf,NULL,NULL);

		sprintf ( buf , "taguser%" PRId32,i);
		const char *tagUser = r->getString( buf,NULL,"admin");

		sprintf ( buf , "tagtime%" PRId32,i);
		int32_t  tagTime = r->getLong(buf,now);

		sprintf ( buf , "tagip%" PRId32,i);
		int32_t  tagIp   = r->getLong(buf,ip);

		// get the value of this tag
		sprintf ( buf , "tagdata%" PRId32 , i );
		const char *dataPtr = r->getString ( buf , NULL );

		// get the tag original key
		key128_t key;
		sprintf ( buf , "tagn1key%" PRId32 , i );
		key.n1 = r->getLongLong ( buf, 0 );
		sprintf ( buf , "tagn0key%" PRId32 , i );
		key.n0 = r->getLongLong ( buf, 0LL );

		// for supporting dumping/adding of tagdb using wget
		sprintf ( buf , "tagn1key%" PRId32"b" , i );
		int64_t v1 = r->getLongLong ( buf, key.n1 );
		sprintf ( buf , "tagn0key%" PRId32"b" , i );
		int64_t v0 = r->getLongLong ( buf, key.n0 );
		bool hackKey = ( v1 || v0 );
		key.n1 = v1;
		key.n0 = v0;

		// if empty skip it
		if ( ! dataPtr || ! dataPtr[0] ) {
			continue;
		}

		// everything is now a string
		int32_t dataSize = strlen(dataPtr) + 1;

		// loop over all urls in the url file if provided
		char *up = fou.getBufStart();

		for ( ; ; ) {
			// set url
			char *urlPtr = up;
			// stop if EOF or processed the one url
			if ( ! urlPtr ) break;
			// advance it or NULL it out
			up = fou.getNextLine ( up );
			// null term the url ptr
			if ( up ) up[-1] = '\0';

			// save buffer spot in case we have to rewind
			int32_t saved = m_sbuf.length();

			// . add to tag rdb recs in safebuf
			// . this pushes the rdbid as first byte
			// . mdwmdwmdw
			Tag *tag = m_sbuf.addTag ( urlPtr,
						   tagTypeStr ,
						   tagTime ,
						   tagUser ,
						   tagIp ,
						   dataPtr,
						   dataSize ,
						   RDB_TAGDB,
						   // do not push rdbid into safebuf
						   false ) ;
			// error?
			if ( ! tag ) {
				return false;
			}

			// hack the key
			if ( hackKey ) {
				tag->m_key = key;
			}

			bool deleteOldKey = false;

			// if tag has different key, delete the old one
			if ( key.n1 && tag->m_key != key )
				deleteOldKey = true;
			
			// if del was marked, delete old one and do not add new one
			if ( deltag && deltag[0] ) {
				// rewind over the tag we were about to add
				m_sbuf.setLength ( saved );
				// and add as a delete
				deleteOldKey = true;
			}

			if ( deleteOldKey ) {
				// make it negative
				key128_t delKey = key;
				delKey.n0 &= 0xfffffffffffffffeLL;
				if (! m_sbuf.safeMemcpy((char *)&delKey,
							sizeof(key128_t)))
					return false;
			}
		}
	}

	return true;
}

// to stdout
int32_t TagRec::print ( ) {
	SafeBuf sb;
	printToBuf ( &sb );

	// dump that
	return fprintf( stderr, "%s\n", sb.getBufStart() );
}

bool TagRec::printToBuf (  SafeBuf *sb ) {
	Tag *tag = getFirstTag();

	for ( ; tag ; tag = getNextTag ( tag ) ) {
		if ( tag->m_type == TT_DUP ) continue; 
		tag->printToBuf ( sb );
		sb->pushChar('\n');
	}

	return true;
}

bool TagRec::setFromBuf ( char *p , int32_t bufSize ) {
	// assign to list! but do not free i guess
	m_lists[0].m_list = p;
	m_lists[0].m_listSize = bufSize;
	m_lists[0].m_listEnd = p + bufSize;
	m_lists[0].m_ownData = false;
	m_lists[0].m_lastKeyIsValid = false;
	m_lists[0].m_fixedDataSize = -1;
	m_lists[0].m_useHalfKeys = false;
	m_lists[0].m_ks = sizeof(key128_t);
	m_listPtrs[0] = &m_lists[0];
	m_numListPtrs = 1;

	return true;
}

bool TagRec::serialize ( SafeBuf &dst ) {
	Tag *tag = getFirstTag();
	for ( ; tag ; tag = getNextTag ( tag ) ) {
		if ( tag->m_type == TT_DUP ) continue;
		if ( ! dst.addTag ( tag ) ) return false;
	}
	return true;
}

bool TagRec::printToBufAsAddRequest ( SafeBuf *sb ) {
	Tag *tag = getFirstTag();
	for ( ; tag ; tag = getNextTag ( tag ) ) 
		if ( tag->m_type != TT_DUP ) tag->printToBufAsAddRequest ( sb);
	return true;
}

bool TagRec::printToBufAsXml ( SafeBuf *sb ) {
	Tag *tag = getFirstTag();
	for ( ; tag ; tag = getNextTag ( tag ) )
		if ( tag->m_type != TT_DUP ) tag->printToBufAsXml ( sb );
	return true;
}

bool TagRec::printToBufAsHtml ( SafeBuf *sb , const char *prefix ) {
	Tag *tag = getFirstTag();
	for ( ; tag ; tag = getNextTag ( tag ) ) 
		if ( tag->m_type != TT_DUP ) tag->printToBufAsHtml (sb,prefix);
	return true;
}

bool TagRec::printToBufAsTagVector  ( SafeBuf *sb ) {
	Tag *tag = getFirstTag();
	for ( ; tag ; tag = getNextTag ( tag ) ) 
		if ( tag->m_type != TT_DUP ) tag->printToBufAsTagVector ( sb );
	return true;
}

//
// flags for a TagDescriptor
//

// is the tag a string type?
#define TDF_STRING 0x01
// can we have multiple tags of this type from the same user in the same TagRec?
#define TDF_ARRAY  0x02
// . should we index it?
// . index gbtagjapanese:<score>
// . also index "gbtagjapanese" if score != 0
// . TODO: actually use this
#define TDF_NOINDEX  0x04

class TagDesc {
public:
	const char *m_name;
	char  m_flags;
	// we compute the m_type of each TD on init
	int32_t  m_type;
};

// map the tags to names
static TagDesc s_tagDesc[] = {

	/// @warning
	/// BR: DO NOT REMOVE unused entries as we may have them in our TagDB already,
	///     and removing them will cause missing info in the TagDB dump code
	///     (when clicking 'page info' in search results)

	// title tag and incoming link text of the root page is stored here
	// for determining default venue addresses
	{"roottitles"             ,TDF_STRING|TDF_NOINDEX,0},

	{"manualban"            ,0x00,0},

	{"deep"                 ,0x00,0},

	// we now index this. really we need it for storing into title rec.
	{"site"                 ,TDF_STRING|TDF_ARRAY,0},

	// . this is used to define INDEPENDENT subsites
	// . such INDEPENDENT subsites should never inherit from this tag rec
	// . it is used to handle "homesteading" sites like geocities.com
	//   and the like, and is automatically set by SiteGetter.cpp
	// . if this is 1 then xyz.com/yyyyy/       is considered a subsite
	// . if this is 2 then xyz.com/yyyyy/zzzzz/ is considered a subsite
	// . if this is -1 then no subsite is found
	// . this should never be 0 either
	{"sitepathdepth"        ,0x00,0},

	// . used by XmlDoc::updateTagdb() and also used to determine
	//   if we should index a site in XmlDoc.cpp. to be indexed a site
	//   must be in google, or must have this tag type in its tag rec,
	//   or have some other, soon to be invented, tag
	// . really this is all controlled by url filters table
	// . allow multiple tags of this type from same "user"
	{"authorityinlink"      ,TDF_STRING|TDF_ARRAY,0},

	{"sitenuminlinks"       ,0x00,0},

	// . the first ip we lookup for this domain
	// . this is permanent and should never change
	// . it is used by Spider.cpp to assign a host for throttling
	//   all urls/SpiderRequests from that ip
	// . so if we did change it then that would result in two hosts
	//   doing the throttling, really messing things up
	{"firstip"              ,0x00,0},

	/// @todo ALC only need this until we cater for unknown tags for display (remember titlerec!)
    // As above, we can't remove the following definition unless if we're sure it's not set anymore
    // Anything below this point is unused.
	{"rootlang"             ,TDF_STRING,0},
	{"manualfilter", 0x00, 0},
	{"dateformat", 0x00, 0}, // 1 = american, 2 = european

	{"venueaddress", TDF_STRING|TDF_ARRAY|TDF_NOINDEX, 0},
	{"hascontactinfo", 0x00, 0},
	{"contactaddress", TDF_ARRAY|TDF_NOINDEX, 0},
	{"contactemails", TDF_ARRAY|TDF_NOINDEX, 0},
	{"hascontactform", 0x00, 0},

	{"ingoogle", 0x00, 0},
	{"ingoogleblogs", 0x00, 0},
	{"ingooglenews", 0x00, 0},
	{"abyznewslinks.address", 0x00, 0},

	{"sitenuminlinksuniqueip"  ,0x00,0},
	{"sitenuminlinksuniquecblock"  ,0x00,0},
	{"sitenuminlinkstotal"  ,0x00,0},

	{"comment", TDF_STRING|TDF_NOINDEX, 0},

	{"sitepop"  ,0x00,0},
	{"sitenuminlinksfresh"  ,0x00,0},

	{"pagerank"             ,0x00,0},
	{"ruleset"              ,0x00,0}
};

// . convert "domain_squatter" to ST_DOMAIN_SQUATTER
// . used by CollectionRec::getRegExpNum()
// . tagnameLen is -1 if unknown
int32_t getTagTypeFromStr( const char *tagname , int32_t tagnameLen ) {
	// this is now the hash
	int32_t tagType;
	if ( tagnameLen == -1 ) {
		tagType = hash32n( tagname );
	} else {
		tagType = hash32( tagname, tagnameLen );
	}

	g_tagdb.setHashTable();

	// sanity check, make sure it is a supported tag!
	if ( ! s_ht.getValue ( &tagType ) ) {
		/// @todo ALC we should cater for deprecated/removed tagname here
		/// Probably will be better than waiting for tagdb to be merged before being able to remove old tags
		log( "tagdb: unsupported tagname '%s'", tagname );
		g_process.shutdownAbort(true);
	}

	return tagType;
}

// . convert ST_DOMAIN_SQUATTER to "domain_squatter"
const char *getTagStrFromType ( int32_t tagType ) {
	g_tagdb.setHashTable();

	TagDesc **ptd = (TagDesc **)s_ht.getValue ( &tagType );
	// sanity check
	if ( ! ptd ) {
		log(LOG_ERROR,"%s:%s:%d: Failed to lookup tagType %" PRId32, __FILE__, __func__, __LINE__, tagType);
		return "UNKNOWN";
	}

	// return it
	return (*ptd)->m_name;
}

// a global class extern'd in .h file
Tagdb g_tagdb;
Tagdb g_tagdb2;

// reset rdb and Xmls
void Tagdb::reset() {
	m_rdb.reset();
	m_siteBuf1.purge();
	m_siteBuf2.purge();
}

void Tagdb::setHashTable ( ) {
	ScopedLock sl(s_htMutex);

	if ( s_initialized ) {
		return;
	}


	// the hashtable of TagDescriptors
	if ( ! s_ht.set ( 4, sizeof(TagDesc *), 1024, NULL, 0, false, 0, "tgdbtb" ) ) {
		log( LOG_WARN, "tagdb: Tagdb hash init failed." );
		return;
	}

	// stock it
	int32_t n = (int32_t)sizeof(s_tagDesc) / (int32_t)sizeof(TagDesc);
	for ( int32_t i = 0 ; i < n ; i++ ) {
		TagDesc *td = &s_tagDesc[i];
		const char *s    = td->m_name;
		int32_t  slen = strlen(s);

		// use the same algo that Words.cpp computeWordIds does 
		int32_t h = hash64Lower_a ( s , slen );

		// call it a bad name if already in there
		TagDesc **petd = (TagDesc **)s_ht.getValue ( &h );
		if ( petd ) {
			log( LOG_WARN, "tagdb: Tag %s collides with old tag %s", td->m_name, (*petd)->m_name );
			return;
		}

		// set the type
		td->m_type = h;

		// add it
		s_ht.addKey ( &h , &td );
	}

	s_initialized = true;
}

bool Tagdb::init ( ) {
	// . what's max # of tree nodes?
	// . assume avg tagdb rec size (siteUrl) is about 82 bytes we get:
	// . NOTE: 32 bytes of the 82 are overhead
	int32_t maxTreeNodes = g_conf.m_tagdbMaxTreeMem  / 82;

	// . initialize our own internal rdb
	// . i no longer use cache so changes to tagdb are instant
	// . we still use page cache however, which is good enough!
	return m_rdb.init ( g_hostdb.m_dir               ,
			    "tagdb"                     ,
			    true                       , // dedup same keys?
			    -1                         , // fixed record size
			    -1,//g_conf.m_tagdbMinFilesToMerge   ,
			    g_conf.m_tagdbMaxTreeMem  ,
			    maxTreeNodes               ,
			    // now we balance so Sync.cpp can ordered huge list
			    true                        , // balance tree?
			    0 , //g_conf.m_tagdbMaxCacheMem ,
			    0 , //maxCacheNodes              ,
			    false                      , // half keys?
			    false                      , //m_tagdbSaveCache
			    NULL,//&m_pc                      ,
			    false,  // is titledb
			    true ,  // preload disk page cache
			    sizeof(key128_t),     // key size
			    true ); // bias disk page cache?
}

bool Tagdb::verify ( const char *coll ) {
	const char *rdbName = "Tagdb";
	
	log ( LOG_DEBUG, "db: Verifying %s for coll %s...", rdbName, coll );
	
	g_jobScheduler.disallow_new_jobs();

	Msg5 msg5;
	RdbList list;
	key128_t startKey;
	key128_t endKey;
	startKey.setMin();
	endKey.setMax();
	CollectionRec *cr = g_collectiondb.getRec(coll);
	
	if ( ! msg5.getList ( RDB_TAGDB    ,
			      cr->m_collnum          ,
			      &list         ,
			      (char *)&startKey      ,
			      (char *)&endKey        ,
			      64000         , // minRecSizes   ,
			      true          , // includeTree   ,
			      0             , // max cache age
			      0             , // startFileNum  ,
			      -1            , // numFiles      ,
			      NULL          , // state
			      NULL          , // callback
			      0             , // niceness
			      false         , // err correction?
			      NULL          ,
			      0             ,
			      -1            ,
			      true          ,
			      -1LL          , // syncPoint
			      true          , // isRealMerge
			      true))          // allowPageCache
	{
		g_jobScheduler.allow_new_jobs();
		log(LOG_DEBUG, "tagdb: HEY! it did not block");
		return false;
	}

	int32_t count  = 0;
	int32_t got    = 0;
	for ( list.resetListPtr(); ! list.isExhausted(); list.skipCurrentRecord() ) {
		key128_t k;
		list.getCurrentKey ( &k );
		// skip negative keys
		if ( (k.n0 & 0x01) == 0x00 ) continue;
		count++;

		uint32_t shardNum = getShardNum ( RDB_TAGDB , &k );
		if ( shardNum == getMyShardNum() ) got++;
	}

	if ( got != count ) {
		// tally it up
		g_rebalance.m_numForeignRecs += count - got;
		log (LOG_DEBUG, "tagdb: Out of first %" PRId32" records in %s, only %" PRId32" belong to our group.",
		     count, rdbName, got);
		// exit if NONE, we probably got the wrong data
		if ( got == 0 ) {
			log( "tagdb: Are you sure you have the right data in the right directory? Exiting." );
		}
		log ( "tagdb: Exiting due to %s inconsistency.", rdbName );
		g_jobScheduler.allow_new_jobs();
		return g_conf.m_bypassValidation;
	}

	log ( LOG_DEBUG, "db: %s passed verification successfully for %" PRId32" recs.", rdbName, count );

	// turn threads back on
	g_jobScheduler.allow_new_jobs();

	// if no recs in tagdb, but sitedb exists, convert it
	if ( count > 0 ) return true;

	// DONE
	return true;
}

// . ssssssss ssssssss ssssssss ssssssss  hash of site/url
// . xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx  tagType OR hash of that+user+data
// . xxxxxxxx xxxxxxxx xxxxxxxx xxxxxxxx
key128_t Tagdb::makeStartKey ( const char *site ) {
	key128_t k;
	k.n1 = hash64n ( site );
	// set lower 64 bits of key to hash of this url
	k.n0 = 0;
	return k;
}

key128_t Tagdb::makeEndKey ( const char *site ) {
	key128_t k;
	k.n1 = hash64n ( site );
	// set lower 64 bits of key to hash of this url
	k.n0 = 0xffffffffffffffffLL;
	return k;
}

key128_t Tagdb::makeDomainStartKey ( Url *u ) {
	key128_t k;
	// hash full hostname
	k.n1 = hash64 ( u->getDomain() , u->getDomainLen() );
	// set lower 64 bits of key to hash of this url
	k.n0 = 0;
	return k;
}

key128_t Tagdb::makeDomainEndKey ( Url *u ) {
	key128_t k;
	// hash full hostname
	k.n1 = hash64 ( u->getDomain() , u->getDomainLen() );
	// set lower 64 bits of key to hash of this url
	k.n0 = 0xffffffffffffffffLL;
	return k;
}


///////////////////////////////////////////////
//
// for getting the final TagRec for a url
//
///////////////////////////////////////////////

static bool s_cacheInitialized = false;
static RdbCache s_cache;
static GbMutex s_cacheInitializedMutex;

Msg8a::Msg8a() {
	m_replies  = 0;
	m_requests = 0;
}

Msg8a::~Msg8a ( ) {
	reset();
}
	
void Msg8a::reset() {
	// do no free if in progress, reply may come in and corrupt the mem
	if ( m_replies != m_requests && ! g_process.m_exiting ) { 
		g_process.shutdownAbort(true);
	}
	m_replies  = 0;
	m_requests = 0;
}

const RdbCache* Msg8a::getCache() {
	return &s_cache;
}

// . get records from multiple subdomains of url
// . calls g_udpServer.sendRequest() on each subdomain of url
// . all matching records are merge into a final record
//   i.e. site tags are also propagated accordingly
// . closest matching "site" is used as the "site" (the site url)
bool Msg8a::getTagRec( Url *url, collnum_t collnum, int32_t niceness, void *state, void (*callback)( void * ),
                       TagRec *tagRec ) {
	CollectionRec *cr = g_collectiondb.getRec ( collnum );
	if ( ! cr ) { 
		g_errno = ENOCOLLREC;
		return true;
	}

	// reset tag rec
	tagRec->reset();

	// in use? need to wait before reusing
	if ( m_replies != m_requests ) {g_process.shutdownAbort(true); }

	// then we gotta free the lists if any
	reset();

	m_niceness = niceness;

	m_collnum = collnum;
	m_tagRec   = tagRec;
	m_callback = callback;
	m_state    = state;

	// reset
	m_errno    = 0;
	m_requests = 0;
	m_replies  = 0;
	m_doneLaunching = false;

	// set siteLen to the provided site if it is non-NULL
	int32_t siteLen = 0;
	char *site = NULL;

	// . get the site
	// . msge0 passes this in as NULL an expects us to figure it out
	// . if site was NULL that means we guess it. default to hostname
	//   unless in a recognized for like /~mwells/
	{
		SiteGetter sg;
		sg.getSite ( url->getUrl(), NULL, 0, collnum, m_niceness );
		// if it set it to a recognized site, like ~mwells, then set "site"
		if ( sg.m_siteLen ) {
			site    = sg.m_site;
			siteLen = sg.m_siteLen;
		}
	}

	// if provided site was NULL and not of a ~mwells type of form
	// then default it to hostname
	if ( ! site || siteLen <= 0 ) {
		site    = url->getHost();
		siteLen = url->getHostLen();
	}

	// if still the host is bad, then forget it
	if ( ! site || siteLen <= 0 ) {
		log("tagdb: got bad url with no site");
		m_errno = EBADURL;
		g_errno = EBADURL;
		return true;
	}

	// temp null terminate it
	char c = site[siteLen];
	site[siteLen] = '\0';

	// use that
	m_siteStartKey = g_tagdb.makeStartKey( site );
	m_siteEndKey = g_tagdb.makeEndKey( site );

	// un NULL terminate it
	site[siteLen] = c;

	m_url = url;

	// point to url
	char *u    = url->getUrl();
	int32_t  ulen = url->getUrlLen();

	// point to the TLD of the url
	const char *tld  = url->getTLD();

	// . if NULL, that is bad... TLD is unsupported
	// . no! it could be an ip address!
	// . anyway, if the tld does not exist, just return an empty tagrec
	//   do not set g_errno
	if ( ! tld && ! url->isIp() ) {
		return true;
	}

	// url cannot have NULLs in it because handleRequest8a() uses
	// strlen() on it to get its size
	for ( int32_t i = 0 ; i < ulen ; i++ ) {
		if ( u[i] ) continue;
		log("TagRec: got bad url with NULL in it %s",u);
		m_errno = EBADURL;
		g_errno = EBADURL;
		return true;
	}

	// get the domain
	m_dom = url->getDomain();

	// if none, bad!
	if ( ! m_dom && ! url->isIp() ) {
		return true;
	}

	// . save ptr for launchGetRequests()
	// . move this BACKWARDS for subdomains that have a ton of .'s
	// . no, now move towards domain
	m_p = m_url->getHost();

	// and save this too
	m_hostEnd = m_url->getHost() + m_url->getHostLen();

	// launch the requests
	if ( ! launchGetRequests() ) return false;

	// . they did it without blocking
	// . this sets g_errno on error
	gotAllReplies();

	// did not block
	return true;
}

struct Msg8aState {
	Msg8aState(Msg8a *msg8a, key128_t startKey, key128_t endKey, int32_t requestNum)
	  : m_msg8a(msg8a)
	  , m_startKey(startKey)
	  , m_endKey(endKey)
	  , m_requestNum(requestNum) {
	}

	Msg8a *m_msg8a;
	key128_t m_startKey;
	key128_t m_endKey;
	int32_t m_requestNum;
};

// . returns false if blocked, true otherwise
// . sets g_errno and returns true on error
bool Msg8a::launchGetRequests ( ) {
	// clear it
	g_errno = 0;
	bool tryDomain = false;

 loop:
	// return true if nothing to launch
	if ( m_doneLaunching )
		return (m_requests == m_replies);

	// don't bother if already got an error
	if ( m_errno )
		return (m_requests == m_replies);

	// limit max to 5ish
	if (m_requests >= MAX_TAGDB_REQUESTS)
		return (m_requests == m_replies);

	// take a breath
	QUICKPOLL(m_niceness);

	key128_t startKey ;
	key128_t endKey   ;

	if ( tryDomain ) {
		startKey = g_tagdb.makeDomainStartKey ( m_url );
		endKey   = g_tagdb.makeDomainEndKey   ( m_url );
		log( LOG_DEBUG, "tagdb: looking up domain tags for %.*s", m_url->getDomainLen(), m_url->getDomain() );
	}
	else {
		// usually the site is the hostname but sometimes it is like
		// "www.last.fm/user/breendaxx/"
		startKey = m_siteStartKey;
		endKey   = m_siteEndKey;

		log( LOG_DEBUG, "tagdb: looking up site tags for %s", m_url->getUrl() );
	}

	// initialize cache
	ScopedLock sl(s_cacheInitializedMutex);
	if ( !s_cacheInitialized ) {
		int64_t maxCacheSize = g_conf.m_tagRecCacheSize;
		int64_t maxCacheNodes = ( maxCacheSize / 200 );

		s_cacheInitialized = true;
		s_cache.init( maxCacheSize, -1, true, maxCacheNodes, false, "tagreccache", false, 16, 16, -1 );
	}
	sl.unlock();

	// get the next mcast
	Msg0 *m = &m_msg0s[m_requests];

	// and the list
	RdbList *listPtr = &m_tagRec->m_lists[m_requests];

	// try to get from cache
	RdbCacheLock rcl(s_cache);
	if ( s_cache.getList( m_collnum, (char*)&startKey, (char*)&startKey, listPtr, true,
	                      g_conf.m_tagRecCacheMaxAge, true) ) {
		// got from cache
		log( LOG_DEBUG, "tagdb: got key=%s from cache", KEYSTR(&startKey, sizeof(startKey)) );

		rcl.unlock();
		m_requests++;
		m_replies++;
	} else {
		rcl.unlock();
		// bias based on the top 64 bits which is the hash of the "site" now
		int32_t shardNum = getShardNum ( RDB_TAGDB , &startKey );
		Host *firstHost ;

		// if niceness 0 can't pick noquery host.
		// if niceness 1 can't pick nospider host.
		firstHost = g_hostdb.getLeastLoadedInShard ( shardNum , m_niceness );
		int32_t firstHostId = firstHost->m_hostId;

		Msg8aState *state = NULL;
		try {
			state = new Msg8aState(this, startKey, endKey, m_requests);
		} catch (...) {
			g_errno = ENOMEM;
			log(LOG_WARN, "tagdb: unable to allocate memory for Msg8aState");
			return false;
		}
		mnew(state, sizeof(*state), "msg8astate");

		// . launch this request, even if to ourselves
		// . TODO: just use msg0!!
		bool status = m->getList ( firstHostId     , // hostId
		                           0          , // ip
		                           0          , // port
		                           0          , // maxCacheAge
		                           false      , // addToCache
		                           RDB_TAGDB  ,
		                           m_collnum     ,
		                           listPtr    ,
		                           (char *) &startKey  ,
		                           (char *) &endKey    ,
		                           10000000            , // minRecSizes
		                           state                , // state
		                           gotMsg0ReplyWrapper ,
		                           m_niceness          ,
		                           true                , // error correction?
		                           true                , // include tree?
		                           true                , // doMerge?
		                           firstHostId         , // firstHostId
		                           0                   , // startFileNum
		                           -1                  , // numFiles
		                           msg0_getlist_infinite_timeout );// timeout

		// error?
		if ( status && g_errno ) {
			// g_errno should be set, we had an error
			m_errno = g_errno;
			return (m_requests == m_replies);
		}

		// successfully launched
		m_requests++;

		// if we got a reply instantly
		if ( status ) {
			m_replies++;
		}
	}

	if ( ! tryDomain ) {
		tryDomain = true;
		goto loop;
	}

	//
	// no more looping!
	//
	// i don't think we need to loop any more because we got all the
	// tags for this hostname. then the lower bits of the Tag key
	// corresponds to the actual SITE hash. so we gotta filter those
	// out i guess after we read the whole list.
	//
	m_doneLaunching = true;

	return (m_requests == m_replies);
}
	
static void gotMsg0ReplyWrapper ( void *state ) {
	Msg8aState *msg8aState = (Msg8aState*)state;

	Msg8a *msg8a = msg8aState->m_msg8a;
	int32_t requestNum = msg8aState->m_requestNum;
	key128_t startKey = msg8aState->m_startKey;
	key128_t endKey = msg8aState->m_endKey;
	mdelete( msg8aState, sizeof(*msg8aState), "msg8astate" );
	delete msg8aState;

	// we got one
	msg8a->m_replies++;

	// error?
	if ( g_errno ) {
		msg8a->m_errno = g_errno;
	} else {
		log( LOG_DEBUG, "tagdb: adding key=%s to cache", KEYSTR(&startKey, sizeof(startKey)) );

		// only add to cache when we don't have error for this reply
		RdbList *list = &(msg8a->m_tagRec->m_lists[requestNum]);

		/// @todo hack to get addList working (verify if there will be issue)
		list->setLastKey((char*)&endKey);

		RdbCacheLock rcl(s_cache);
		s_cache.addList( msg8a->m_collnum, (char*)&startKey, list);
	}

	// launchGetRequests() returns false if still waiting for replies...
	if ( ! msg8a->launchGetRequests() ) {
		return;
	}

	// get all the replies
	msg8a->gotAllReplies();

	// set g_errno for the callback
	if ( msg8a->m_errno ) {
		g_errno = msg8a->m_errno;
	}

	// otherwise, call callback
	msg8a->m_callback ( msg8a->m_state );
}

// get the TagRec from the reply
void Msg8a::gotAllReplies ( ) {
	// if any had an error, don't do anything
	if ( m_errno ) {
		return;
	}

	// scan the lists
	for ( int32_t i = 0 ; i < m_replies ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);

		// get list
		RdbList *list = &m_tagRec->m_lists[i];

		// skip if empty
		if ( list->m_listSize <= 0 ) {
			continue;
		}

		// panic msg
		if ( list->m_listSize >= 10000000 ) {
			log("tagdb: CAUTION!!! cutoff tagdb list!");
			log("tagdb: CAUTION!!! will lost useful info!!");
			g_process.shutdownAbort(true);
		}

		// otherwise, add to array
		m_tagRec->m_listPtrs[m_tagRec->m_numListPtrs] = list;

		// advance
		m_tagRec->m_numListPtrs++;
	}

	// scan tags in list and set Tag::m_type to TT_DUP if its a dup
	HashTableX cx;
	char cbuf[2048];
	cx.set ( 4, 0, 64, cbuf, 2048, false, m_niceness, "tagtypetab" );

	Tag *tag = m_tagRec->getFirstTag();

	// . loop over all tags in all lists in order by key
	// . each list should be from a different suburl?
	// . the first list should be the narrowest/longest?
	for ( ; tag ; tag = m_tagRec->getNextTag ( tag ) ) {
		// breathe
		QUICKPOLL(m_niceness);

		// form the hash!
		uint32_t h32 = (uint32_t)((tag->m_key.n0) >> 32);

		// otherwise, record it
		if ( cx.isInTable(&h32 ) ) {
			tag->m_type = TT_DUP;
		} else if ( ! cx.addKey(&h32) ) {
			m_errno = g_errno;
			return;
		}
	}
}		

///////////////////////////////////////////////
//
// sendPageTagdb() is the HTML interface to tagdb
//
///////////////////////////////////////////////

static void sendReplyWrapper  ( void *state ) ;
static void sendReplyWrapper2 ( void *state ) ;
static bool sendReply         ( void *state ) ;
static bool sendReply2        ( void *state ) ;
static bool getTagRec ( class State12 *st );

// don't change name to "State" cuz that might conflict with another
class State12 {
public:
	TcpSocket   *m_socket;
	bool         m_adding;
	collnum_t m_collnum;
	bool         m_isLocal;
	HttpRequest  m_r;
	TagRec       m_tagRec;
	TagRec       m_newtr;
	Msg8a        m_msg8a;
	Url          m_url;
	const char  *m_urls;
	int32_t         m_urlsLen;
	Msg1         m_msg1;
	RdbList      m_list;
	int32_t         m_niceness;
	bool         m_mergeTags;
};

// . returns false if blocked, true otherwise
// . sets g_errno on error
// . make a web page displaying the tagdb interface
// . call g_httpServer.sendDynamicPage() to send it
// . show a textarea for sites, then list all the different site tags
//   and have an option to add/delete them
bool sendPageTagdb ( TcpSocket *s , HttpRequest *req ) {
	// get the collection record
	CollectionRec *cr = g_collectiondb.getRec ( req );
	if ( ! cr ) {
		g_errno = ENOCOLLREC;
		log("admin: No collection record found "
		    "for specified collection name. Could not add sites to "
		    "tagdb. Returning HTTP status of 500.");
		    
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		return g_httpServer.sendErrorReply ( s , 500 ,
						  "collection does not exist");
	}

	// make a state
	State12 *st ;
	try { st = new (State12); }
	catch ( ... ) {
		g_errno = ENOMEM;
		log("PageTagdb: new(%" PRId32"): %s",
		    (int32_t)sizeof(State12),mstrerror(g_errno));
		    
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		return g_httpServer.sendErrorReply(s,500,mstrerror(g_errno));
	}
	mnew ( st , sizeof(State12) , "PageTagdb" );

	// assume we've nothing to add
	st->m_adding = false;
	// save the socket
	st->m_socket = s;
	// i guess this is nuked, so copy it
	st->m_r.copy ( req );
	// make it high priority
	st->m_niceness = 0;
	// point to it
	HttpRequest *r = &st->m_r;

	// get the collection
	int32_t  collLen = 0;
	const char *coll  = r->getString ( "c" , &collLen  , NULL /*default*/);
	// get collection rec
	CollectionRec *cr2 = g_collectiondb.getRec ( coll );
	// bitch if no collection rec found
	if ( ! cr2 || ! coll || collLen+1 > MAX_COLL_LEN ) {
		g_errno = ENOCOLLREC;
		log("admin: No collection record found "
		    "for specified collection name. Could not add sites to "
		    "tagdb. Returning HTTP status of 500.");
		mdelete ( st , sizeof(State12) , "PageTagdb" );
		delete (st);
		log(LOG_ERROR,"%s:%s:%d: call sendErrorReply.", __FILE__, __func__, __LINE__);
		return g_httpServer.sendErrorReply ( s , 500 , "collection does not exist");
	}

	// . get fields from cgi field of the requested url
	// . get the null-terminated, newline-separated lists of sites to add
	int32_t  urlsLen = 0;
	const char *urls = (char*)r->getString ( "u" , &urlsLen , NULL /*default*/);
	
	if ( urls ) {
		//a quick hack so we can put multiple sites in a link
		char *u = const_cast<char*>(urls);
		if(r->getLong("uenc", 0))
			for(int32_t i = 0; i < urlsLen; i++)
				if(u[i] == '+')
					u[i] = '\n';
	} else {
		// do not print "(null)" in the textarea
		urls = "";
	}

	// are we coming from a local machine?
	st->m_isLocal = r->isLocal();

	// it references into the request, should be ok
	st->m_collnum = cr->m_collnum;


	// the url buffer
	st->m_urls    = urls;
	st->m_urlsLen = urlsLen;

	int32_t ufuLen;
	const char *ufu = r->getString("ufu",&ufuLen);

	if ( urls[0] == '\0' && ! ufu ) return sendReply ( st );

	const char *get = r->getString ("get",NULL );
	// this is also a get operation but merges the tags from all TagRecs
	const char *merge = r->getString("tags",NULL);

	// is this an add/update operation? or just get?
	if ( get || merge ) st->m_adding = false;
	else                st->m_adding = true;

	// regardless, we have to get the tagrec for all operations
	st->m_url.set( urls );
	st->m_mergeTags = merge;

	return getTagRec ( st );
}

bool getTagRec ( State12 *st ) {
	// this replaces msg8a
	if ( !st->m_msg8a.getTagRec( &st->m_url, st->m_collnum, st->m_niceness, st, sendReplyWrapper, &st->m_tagRec ) )
		return false;

	return sendReply ( st );
}

void sendReplyWrapper ( void *state ) {
	sendReply ( state );
}

static void sendReplyWrapper2 ( void *state ) {
	State12 *st = (State12 *)state;
	// re-get the tags from msg8a since we changed them
	getTagRec(st);
}

bool sendReply ( void *state ) {
	// get our state class
	State12 *st = (State12 *) state;
	// get the request
	HttpRequest *r = &st->m_r;
	// and socket
	TcpSocket *s = st->m_socket;

	if ( ! st->m_adding ) return sendReply2 ( st );

	// no permmission?
	bool isMasterAdmin = g_conf.isMasterAdmin ( s , r );
	bool isCollAdmin = g_conf.isCollAdmin ( s , r );
	if ( ! isMasterAdmin && ! isCollAdmin ) {
		g_errno = ENOPERM;
		return sendReply2 ( st );
	}

	TagRec *newtr = &st->m_newtr;
	// update it from the http request
	newtr->setFromHttpRequest ( r , s );

	// shrotcut
	SafeBuf *sbuf = &newtr->m_sbuf;

	// use the list we got
	RdbList *list = &st->m_list;
	key128_t startKey;
	key128_t endKey;
	startKey.setMin();
	endKey.setMax();

	// set it from safe buf
	list->set ( sbuf->getBufStart() ,
		    sbuf->length() ,
		    NULL ,
		    0 ,
		    (char *)&startKey ,
		    (char *)&endKey  ,
		    -1 ,
		    false ,
		    false ,
		    sizeof(key128_t) );

	// no longer adding
	st->m_adding = false;

	// . just use TagRec::m_msg1 now
	// . no, can't use that because tags are added using SafeBuf::addTag()
	//   which first pushes the rdbid, so we gotta use msg4
	if ( ! st->m_msg1.addList ( list ,
				    RDB_TAGDB ,
				    st->m_collnum ,
				    st ,
				    sendReplyWrapper2 ,
				    false ,
				    st->m_niceness ) )
		return false;

	// . if addTagRecs() doesn't block then sendReply right away
	// . this returns false if blocks, true otherwise
	return getTagRec ( st );
}

bool sendReply2 ( void *state ) {

	// get our state class
	State12 *st = (State12 *) state;
	// get the request
	HttpRequest *r = &st->m_r;
	// and socket
	TcpSocket *s = st->m_socket;

	// page is not more than 32k
	char buf[1024*32];
	SafeBuf sb(buf, 1024*32);
	// do they want an xml reply?
	if( r->getLong("xml",0) ) {
		sb.safePrintf("<?xml version=\"1.0\" "
			      "encoding=\"ISO-8859-1\"?>\n"
			      "<response>\n");
	
		st->m_tagRec.printToBufAsXml(&sb);
		
		sb.safePrintf("</response>");
		log ( LOG_INFO,"sending raw page###\n");
		// clear g_errno, if any, so our reply send goes through
		g_errno = 0;
		// extract the socket
		TcpSocket *s = st->m_socket;
		// . nuke the state
		mdelete(st, sizeof(State12), "PageTagdb");
		delete (st);
		// . send this page
		// . encapsulates in html header and tail
		// . make a Mime
		return g_httpServer.sendDynamicPage(s, sb.getBufStart(), 
                                                    sb.length(),
						    0, false, "text/xml",
						    -1, NULL, "ISO-8859-1");
	}
	// . print standard header
	// . do not print big links if only an assassin, just print host ids
	g_pages.printAdminTop ( &sb, st->m_socket , &st->m_r );
	// did we add some sites???
	if ( st->m_adding ) {
		// if there was an error let them know
		if ( g_errno )
			sb.safePrintf("<center>Error adding site(s): <b>"
				      "%s[%i]</b><br><br></center>\n",
				      mstrerror(g_errno) , g_errno );
		else   sb.safePrintf ("<center><b><font color=red>"
				      "Sites added successfully"
				      "</font></b><br><br></center>\n");
	}

	//char *c = st->m_coll;
	char bb [ MAX_COLL_LEN + 60 ];
	bb[0]='\0';

	sb.safePrintf(
		      "<style>"
		      ".poo { background-color:#%s;}\n"
		      "</style>\n" ,
		      LIGHT_BLUE );

	// print interface to add sites
	sb.safePrintf (
		  "<table %s>"
		  "<tr><td colspan=2>"
		  "<center><b>Tagdb</b>%s</center>"
		  "</td></tr>", TABLE_STYLE , bb );

	// sometimes we add a huge # of urls, so don't display them because
	// it like freezes the silly browser
	const char *uu = st->m_urls;
	if ( st->m_urlsLen > 100000 ) uu = "";

	sb.safePrintf ( "<tr class=poo><td>"
			"<b>urls</b>"
			"<br>"

			"<font size=-2>"
			"Enter a single URL and then click <i>Get Tags</i> to "
			"get back its tags. Enter multiple URLs and select "
			"the tags names and values in the other table "
			"below in order to tag "
			"them all with those tags when you click "
			"<i>Add Tags</i>. "
			"On the command line you can also issue a "
			"<i>./gb 0 dump S main 0 -1 1</i>"
			"command, for instance, to dump out the tagdb "
			"contents for the <i>main</i> collection on "
			"<i>host #0</i>. "
			"</font>"


			"</td>");

	sb.safePrintf (""
		       "<td width=70%%>"
		       "<br>"
		       "<textarea rows=16 cols=64 name=u>"
		       "%s</textarea></td></tr>" , uu );

	// allow filename to load them from
	sb.safePrintf("<tr class=poo>"
		      "<td>"
		      "<b>file of urls to tag</b>"
		      "<br>"
		      "<font size=-2>"
		      "If provided, Gigablast will read the URLs from "
		      "this file as if you pasted them into the text "
		      "area above. The text area will also be ignored."
		      "</font>"
		      "</td>"
		      "<td><input name=ufu "
		      "type=text size=40>"
		      "</td></tr>"
		      );

	// this is applied to every tag that is added for accountability
	sb.safePrintf("<tr class=poo><td>"
		      "<b>username</b>"
		      "<br><font size=-2>"
		      "Stored with each tag you add for accountability."
		      "</font>"
		      "</td><td>"
		      "<input name=username type=text size=6 "
		      "value=\"admin\"> " 
		      "</td></tr>"
		      );

	// as a safety, this must be checked for any delete operation
	sb.safePrintf ("<tr class=poo><td><b>delete operation</b>"
		       "<br>"
		       "<font size=-2>"

			"If checked "
			"then the tag names you specify below will be "
			"deleted for the URLs you provide in the text area "
			"when you click <i>Add Tags</i>."
		       "</font>"


		       "</td><td><input type=\"checkbox\" "
		       "value=\"1\" name=\"delop\"></td></tr>");

	// close up
	sb.safePrintf ("<tr bgcolor=#%s><td colspan=2>"
		       "<center>"
		       // this is merge all by default right now but since
		       // zak is really only using eventtaghashxxxx.com we
		       // should be ok
		       "<input type=submit name=get "
		       "value=\"Get Tags\" border=0>"
		       "</center>"
		       "</td></tr></table>"
		       "<br><br>"
		       , DARK_BLUE
		       );


	// . show all tags we got values for
	// . put a delete checkbox next to each one
	// . show 5-10 dropdowns for adding new tags

	// for some reason the "selected" option tags do not show up below
	// on firefox unless i have this line.

	sb.safePrintf (
		       "<table %s>"
		       "<tr><td colspan=20>"
		       "<center><b>Add Tag</b></center>"
		       "</td></tr>", TABLE_STYLE );


	// count how many "tagRecs" we are taking tags from
	Tag *jtag  = st->m_tagRec.getFirstTag();
	int32_t numTagRecs = 0;
	for ( ; jtag ; jtag = st->m_tagRec.getNextTag(jtag) ) {
		// skip dups
		if ( jtag->m_type == TT_DUP ) continue;
		// count # of TagRecs contributing to the tags
		if ( jtag && jtag->isType("site") ) numTagRecs++;
	}

	// if we are displaying a COMBINATION of TagRecs merged together in 
	// the inheritance loop (above) then you can not edit that! you can
	// only edit individual tag recs
	bool canEdit = (numTagRecs <= 1);

	if ( ! canEdit )
		sb.safePrintf("<tr class=poo>"
			      "<td colspan=10><center><font color=red>"
			      "<b>Can not edit because more than one "
			      "TagRecs were merged</b></font></center>"
			      "</td></tr>\n" );

	// headers
	sb.safePrintf("<tr bgcolor=#%s>"
		      "<td><b>del?</b></td>"
		      "<td><b>tag name</b></td>"
		      "<td><b>tag value</b></td>"
		      "<td><b>datasize (with NULL)</b></td>"
		      "<td><b>username</b></td>"
		      "<td><b>timestamp</b></td>"
		      "<td><b>user ip</b></td>"
		      "<td><b>deduphash32</b></td>"
		      "<td><b>sitehash32</b></td>"
		      "</tr>\n",
		      DARK_BLUE);

	// set up the loop
	Tag *itag  = st->m_tagRec.getFirstTag();
	int32_t count = 0;
	int32_t empty = 0;
	// loop over all tags in TagRec
	for ( ; empty < 3 ; ++count ) {
		// use this tag to print from
		Tag *ctag = itag;

		// advance
		if ( itag ) {
			itag = st->m_tagRec.getNextTag(itag);
		}

		// make it NULL, do not start over at the beginning
		if ( empty > 0 ) {
			ctag = NULL;
		}

		// skip dups
		if ( ctag && ctag->m_type == TT_DUP ) {
			--count;
			continue;
		}

		// if ctag NULL and we are getting all tags, break
		if ( ! canEdit && ! ctag ) {
			break;
		}

		// if we are NULL, print out 3 empty tags
		if ( ! ctag ) {
			empty++;
		}

		// start the section
		sb.safePrintf("<tr class=poo>");

		// the delete tag checkbox
		sb.safePrintf("<td>");

		if ( ctag && canEdit ) {
			sb.safePrintf("<input name=deltag%" PRId32" type=checkbox>", count);
		} else {
			sb.safePrintf("&nbsp;");
		}

		sb.safePrintf("</td>");

		// start the next cell
		sb.safePrintf("<td>");

		// print drop down
		if ( ! ctag ) {
			sb.safePrintf("<select name=tagtype%" PRId32">", count);
		}

		// how many tags do we have?
		int32_t n = (int32_t)sizeof(s_tagDesc)/(int32_t)sizeof(TagDesc);
		// the options
		for ( int32_t i = 0 ; ! ctag && i < n ; i++ ) {
			TagDesc *td = &s_tagDesc[i];
			// get tag name
			const char *tagName = td->m_name;

			// select the item in the dropdown
			const char *selected = "";
			// was it selected?
			if ( ctag && td->m_type == ctag->m_type ) 
				selected = " selected";
			// show it in the drop down list
			sb.safePrintf("<option value=\"%s\"%s>%s",
				      tagName,selected,tagName);
		}

		// close up the drop down list
		if ( ! ctag ) {
			sb.safePrintf("</select>");
		} else {
			const char *tagName = getTagStrFromType ( ctag->m_type );
			sb.safePrintf("<input type=hidden name=tagtype%" PRId32" "
				      "value=\"%s\">%s",
				      count,tagName,tagName);
		}
		sb.safePrintf("</td><td>");

		// the score field for the drop down list, whatever tag id
		// was selected will have this score
		if ( canEdit ) {
			sb.safePrintf( "<input type=text name=tagdata%" PRId32" size=50 value=\"", count );
		}

		// show the value
		if ( ctag ) {
			ctag->printDataToBuf( &sb );
		}

		// close up the input tag
		if ( canEdit ) {
			sb.safePrintf( "\">" );
		}

		// close up table cell
		sb.safePrintf("\n</td>");

		// if no tag, just placeholders
		if ( ! ctag ) {
			sb.safePrintf("<td>&nbsp;</td>"
				      "<td>&nbsp;</td>"
				      "<td>&nbsp;</td>"
				      "<td>&nbsp;</td>"
				      "<td>&nbsp;</td>"
				      "<td>&nbsp;</td></tr>");
			continue;
		}

		// data size
		sb.safePrintf("<td>%" PRId32"</td>",(int32_t)ctag->getTagDataSize());

		// username, timestamp only for non-empty tags
		char *username = ctag->getUser();
		int32_t timestamp = ctag->m_timestamp;
		int32_t  ip  = 0;
		const char *ips = "&nbsp;";
		if ( ctag->m_ip ) {
			ip=ctag->m_ip;
			ips=iptoa(ctag->m_ip);
		}

		// convert timestamp to string
		char tmp[64];
		sprintf(tmp,"&nbsp;");
		time_t ts = timestamp;
		struct tm tm_buf;
		struct tm *timeStruct = localtime_r(&ts,&tm_buf);
		if ( timestamp ) {
			strftime(tmp,64,"%b-%d-%Y-%H:%M:%S",timeStruct);
		}

		sb.safePrintf("<td><input type=hidden name=taguser%" PRId32" value=%s>%s</td>",
			      count,username,username);
		sb.safePrintf("<td><input type=hidden name=tagtime%" PRId32" value=%" PRId32">%s</td>",
			      count,timestamp,tmp);

		sb.safePrintf("<td><input type=hidden name=tagip%" PRId32" value=%" PRId32">%s",
			      count,ip,ips);

		sb.safePrintf("<input type=hidden name=tagn1key%" PRId32" value=%" PRIu64">",
			      count,ctag->m_key.n1);
		sb.safePrintf("<input type=hidden name=tagn0key%" PRId32" value=%" PRIu64">",
			      count,ctag->m_key.n0);

		sb.safePrintf("</td>");

		sb.safePrintf("<td>0x%" PRIx32"</td>", (int32_t)(ctag->m_key.n0>>32) );

		sb.safePrintf("<td>0x%" PRIx32"</td>",
			      // order 1 in since we always do that because
			      // we forgot to shift up one for the delbit
			      // above in Tag::set() when it sets m_key.n0
			      (int32_t)(ctag->m_key.n0&0xffffffff) | 0x01);

		sb.safePrintf("</tr>");
	}

	// do not print add or del tags buttons if we got tags from more
	// than one TagRec!
	if ( canEdit ) {
		sb.safePrintf( "<tr bgcolor=#%s><td colspan=10><center>"
		               "<input type=submit name=add "
		               "value=\"Add Tags\" border=0>"
		               "</center></td>"
		               "</tr>\n", DARK_BLUE );
	}

	sb.safePrintf ( "</center></table>" );
	sb.safePrintf ("</form>");
	sb.safePrintf ("</html>");
	
	// clear g_errno, if any, so our reply send goes through
	g_errno = 0;

	// . nuke the state
	mdelete ( st , sizeof(State12) , "PageTagdb" );
	delete (st);

	// . send this page
	// . encapsulates in html header and tail
	// . make a Mime
	return g_httpServer.sendDynamicPage (s, sb.getBufStart(), sb.length());
}

// . we can have multiple tags of this type per tag for a single username
// . by default, there can be multiple tags of the same type in the Tag as
//   int32_t as the usernames are all different. see addTag()'s deduping below.
static bool isTagTypeUnique ( int32_t tt ) {
	// a dup?
	if ( tt == TT_DUP ) return false; // TT_DUP = 123456
	// make sure table is valid
	g_tagdb.setHashTable();
	// look up in hash table
	TagDesc **tdp = (TagDesc **)s_ht.getValue ( &tt );
	if ( ! tdp ) {
		log("tagdb: tag desc is NULL for tag type %" PRId32" assuming "
		    "not indexable",tt);
		return false;
	}
	// do not core for now
	TagDesc *td = *tdp;
	if ( ! td ) {
		log("tagdb: got unknown tag type %" PRId32" assuming "
		    "unique",tt);
		return true;
	}
	// if none, that is crazy
	if ( ! td ) { g_process.shutdownAbort(true); }
	// return 
	if ( td->m_flags & TDF_ARRAY) return false;
	return true;
}

// used to determine if one Tag should overwrite the other! if they
// have the same dedup hash... then yes...
int32_t Tag::getDedupHash ( ) {

	// if unique use that!
	if ( isTagTypeUnique ( m_type ) ) return m_type;

	// if we are NOT unique... then hash username and data. thus we only
	// replace a key if its the same tagtype, username and data. that
	// way it will just update the timestamp and/or ip.

	// start hashing here
	char *startHashing = (char *)&m_type;
	// end here. include username (and tag data!)
	char *endHashing = m_buf + m_bufSize;

	// do not include bufsize in hash
	int32_t saved = m_bufSize;
	m_bufSize = 0;

	// hash this many bytes
	int32_t hashSize = endHashing - startHashing;
	// set key
	int32_t dh = hash32 ( startHashing , hashSize );

	// revert bufsize
	m_bufSize = saved;

	return dh;
}

// make sure sizeof(Entry2)=5 not 8!
#pragma pack(1)

class Entry1 {
public:
	uint32_t m_hostHash32;
	uint32_t m_siteNumInlinksUniqueCBlock;
};

class Entry2 {
public:
	uint32_t m_hostHash32;
	uint8_t  m_siteNumInlinksUniqueCBlock;
};

static int linkSort1Cmp ( const void *a, const void *b ) {
	Entry1 *ea = (Entry1 *)a;
	Entry1 *eb = (Entry1 *)b;
	if ( ea->m_hostHash32 > eb->m_hostHash32 ) return  1;
	if ( ea->m_hostHash32 < eb->m_hostHash32 ) return -1;
	return 0;
}

static int linkSort2Cmp ( const void *a, const void *b ) {
	Entry2 *ea = (Entry2 *)a;
	Entry2 *eb = (Entry2 *)b;
	if ( ea->m_hostHash32 > eb->m_hostHash32 ) return  1;
	if ( ea->m_hostHash32 < eb->m_hostHash32 ) return -1;
	return 0;
}

bool Tagdb::loadMinSiteInlinksBuffer ( ) {

	if ( ! loadMinSiteInlinksBuffer2() ) return false;

	// sanity testing
	uint32_t hostHash32 = hash32n("www.imdb.com");
	int32_t msi = getMinSiteInlinks ( hostHash32 );
	if ( msi < 10 ) {
		log("tagdb: bad siteinlinks. linkedin.com not found.");
		//return false;
	}
	hostHash32 = hash32n("0009.org" );
	msi = getMinSiteInlinks ( hostHash32 );
	if ( msi < 0 ) 	{
		log("tagdb: bad siteinlinks. 0009.org not found.");
		//return false;
	}
	// slot #1 in the buffer. make sure b-stepping doesn't lose it between
	// the roundoff error cracks.
	hostHash32 = hash32n("www.hindu.com");
	msi = getMinSiteInlinks ( hostHash32 );
	if ( msi < 3 ) 	{
		log("tagdb: bad siteinlinks. www.hindu.com not found "
		    "(%" PRId32").",
		    hostHash32);
		//return false;
	}

	Url tmp;
	tmp.set("gnu.org");
	hostHash32 = tmp.getHash32WithWWW();
	msi = getMinSiteInlinks ( hostHash32 );
	if ( msi < 0 ) 	{
		log("tagdb: bad siteinlinks. www.gnu.org not found.");
		//return false;
	}

	
	return true;
}

bool Tagdb::loadMinSiteInlinksBuffer2 ( ) {

	// use 4 bytes for the first 130,000 entries or so to hold
	// # of site inlinks. then we only need 1 byte since the remaining
	// 25M are <256 sitenuminlinksunqiecblocks
	m_siteBuf1.load(g_hostdb.m_dir,"sitelinks1.dat","stelnks1");
	m_siteBuf2.load(g_hostdb.m_dir,"sitelinks2.dat","stelnks2");

	m_siteBuf1.setLabel("sitelnks");
	m_siteBuf2.setLabel("sitelnks");

	if ( m_siteBuf1.length() > 0 &&
	     m_siteBuf2.length() > 0 ) 
		return true;

	log("gb: loading %ssitelinks.txt",g_hostdb.m_dir);

	// ok, make it
	SafeBuf tmp;
	tmp.load(g_hostdb.m_dir,"sitelinks.txt");
	if ( tmp.length() <= 0 ) {
		log("gb: fatal error. could not find required file "
		    "./sitelinks.txt");
		return false;
	}

	log("gb: starting initial creation of sitelinks1.dat and "
	    "sitelinks2.dat files");

	// now parse each line in that
	char *p = tmp.getBufStart();
	char *pend = p + tmp.length();
	char *newp = NULL;
	SafeBuf buf1;
	SafeBuf buf2;
	int32_t count = 0;
	for ( ; p < pend ; p = newp ) {
		
		if ( ++count % 1000000 == 0 )
			log("gb: parsing line # %" PRId32,count);

		// advance to next line
		newp = p;
		for ( ; newp < pend && *newp != '\n' ; newp++ );
		if ( newp < pend ) newp++;
		// parse this line
		int32_t numLinks = atoi(p);
		// skip number
		for ( ; *p && *p != ' ' && *p != '\n' ; p++ );
		// strange
		if ( ! *p || *p == '\n' ) continue;
		// skip spaces
		for ( ; *p == ' ' ; p++ );
		// get hostname
		char *host = p;
		// find end of it
		for ( ; *p && *p != '\n' && *p != ' ' && *p != '\t' ; p++ );
		// hash it
		uint32_t hostHash32 = hash32 ( host , p - host );

		// store in buffer
		if ( numLinks >= 256 ) {
			Entry1 e1;
			e1.m_siteNumInlinksUniqueCBlock = numLinks;
			e1.m_hostHash32 = hostHash32;
			buf1.safeMemcpy ( &e1 , sizeof(Entry1) );
		}
		else {
			Entry2 e2;
			e2.m_siteNumInlinksUniqueCBlock = numLinks;
			e2.m_hostHash32 = hostHash32;
			buf2.safeMemcpy ( &e2 , sizeof(Entry2) );
		}
	}		

	log("gb: sorting sitelink data");

	// now sort each one
	qsort ( buf1.getBufStart() , 
		buf1.length()/sizeof(Entry1),
		sizeof(Entry1),
		linkSort1Cmp );

	qsort ( buf2.getBufStart() , 
		buf2.length()/sizeof(Entry2),
		sizeof(Entry2),
		linkSort2Cmp );


	// now copy to the official buffer so we only alloc what we need
	m_siteBuf1.safeMemcpy ( &buf1 );
	m_siteBuf2.safeMemcpy ( &buf2 );

	log("gb: saving sitelinks1.dat and sitelinks2.dat");

	m_siteBuf1.save(g_hostdb.m_dir,"sitelinks1.dat");
	m_siteBuf2.save(g_hostdb.m_dir,"sitelinks2.dat");

	return true;
}

int32_t Tagdb::getMinSiteInlinks ( uint32_t hostHash32 ) {

	if ( m_siteBuf1.length() <= 0 ) { 
		log("tagdb: load not called");
		g_process.shutdownAbort(true); 
	}

	// first check buf1 doing bstep
	int32_t ne = m_siteBuf1.length() / sizeof(Entry1);
	Entry1 *ep = (Entry1 *)m_siteBuf1.getBufStart();
	Entry2 *fp = NULL;
	int32_t i = ne / 2;
	int32_t step = ne / 2;
	int32_t dir = 0;

 loop1:

	if ( i < 0 ) i = 0;
	if ( i >= ne ) i = ne-1;

	step /= 2;

	if ( step == 1 )
		goto linearScan1;
	if ( hostHash32 < ep[i].m_hostHash32 ) {
		i -= step;
		goto loop1;
	}
	if ( hostHash32 > ep[i].m_hostHash32 ) {
		i += step;
		goto loop1;
	}
	return ep[i].m_siteNumInlinksUniqueCBlock;

 linearScan1:
	if ( hostHash32 < ep[i].m_hostHash32 ) {
		if ( i == 0 ) goto tryNextBuf;
		if ( dir == +1 ) goto tryNextBuf;
		i--;
		dir = -1;
		goto linearScan1;
	}
	if ( hostHash32 > ep[i].m_hostHash32 ) {
		if ( i == ne-1 ) goto tryNextBuf;
		if ( dir == -1 ) goto tryNextBuf;
		i++;
		dir = +1;
		goto linearScan1;
	}
	return ep[i].m_siteNumInlinksUniqueCBlock;


 tryNextBuf:

	// reset parms
	ne = m_siteBuf2.length() / sizeof(Entry2);
	fp = (Entry2 *)m_siteBuf2.getBufStart();
	i = ne / 2;
	step = ne / 2;
	dir = 0;

 loop2:

	if ( i < 0 ) i = 0;
	if ( i >= ne ) i = ne-1;
	step /= 2;
	if ( step == 1 )
		goto linearScan2;
	if ( hostHash32 < fp[i].m_hostHash32 ) {
		i -= step;
		goto loop2;
	}
	if ( hostHash32 > fp[i].m_hostHash32 ) {
		i += step;
		goto loop2;
	}
	return fp[i].m_siteNumInlinksUniqueCBlock;

 linearScan2:

	if ( hostHash32 < fp[i].m_hostHash32 ) {
		if ( i == 0    ) return -1;
		if ( dir == +1 ) return -1;
		i--;
		dir = -1;
		goto linearScan2;
	}
	if ( hostHash32 > fp[i].m_hostHash32 ) {
		if ( i == ne-1 ) return -1;
		if ( dir == -1 ) return -1;
		i++;
		dir = +1;
		goto linearScan2;
	}
	return fp[i].m_siteNumInlinksUniqueCBlock;
}
