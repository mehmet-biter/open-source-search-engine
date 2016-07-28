#include "gb-include.h"

#include "Xml.h"

#include "Mem.h"     // mfree(), mmalloc()
#include "Unicode.h" // for html entities that return unicode
#include "Titledb.h"
#include "Words.h"
#include "Pos.h"
#include "Sanity.h"


Xml::Xml  () { 
	m_xml = NULL; 
	m_xmlLen = 0; 
	m_nodes = NULL; 
	m_numNodes=0;
}

// . should free m_xml if m_copy is true
Xml::~Xml () { 
	reset(); 
}

// . for parsing xml conf files
int32_t Xml::getLong ( int32_t n0, int32_t n1, const char *tagName, int32_t defaultLong ) {
	int32_t len;
	char *s = getTextForXmlTag ( n0 , n1 , tagName , &len , false );
	if ( s ) return atol2 ( s , len );
	// return the default if no non-white-space text
	return defaultLong;
}

char *Xml::getString ( int32_t n0, int32_t n1, const char *tagName, int32_t *len ,
		       bool skipLeadingSpaces ) const {
	char *s = getTextForXmlTag ( n0, n1, tagName, len, skipLeadingSpaces );
	if ( s ) return s;
	// return the default if s is null
	return NULL;
}

// . used by getValueAsBool/Long/String()
// . tagName is compound for xml tags, simple for html tags
// . NOTE: we skip over leading spaces
char *Xml::getTextForXmlTag ( int32_t n0, int32_t n1, const char *tagName, int32_t *len,
			      bool skipLeadingSpaces ) const {
	// assume len is 0
	*len = 0;
	// get a matching xml TAG
	int32_t num = getNodeNum ( n0 , n1 , tagName , strlen(tagName) );
	if ( num < 0                 ) return NULL;
	return getString ( num , skipLeadingSpaces , len );
}


char *Xml::getString ( int32_t num , bool skipLeadingSpaces , int32_t *len ) const {
	// get the text of this tag (if any)
	if ( ++num >= m_numNodes     ) { *len = 0; return NULL; }
	if ( ! m_nodes[num].isText() ) { *len = 0; return NULL; }
	// if we don't skip leading spaces return it as is
	if ( ! skipLeadingSpaces ) {
		*len   = m_nodes[num].m_nodeLen;		
		return   m_nodes[num].m_node;
	}

	// get the string
	char *s    = m_nodes[num].m_node;
	// set the length and return the string
	int32_t  slen = m_nodes[num].m_nodeLen;
	// skip leading spaces
	while ( is_wspace_utf8 ( s ) && slen > 0 ) { s++; slen--; }
	// set len
	*len = slen;
	// return NULL if slen is 0
	if ( slen == 0 ) return NULL;
	// otherwise return s
	return s;
}

int32_t Xml::getEndNode ( int32_t num ) {
	if ( (num < 0) || (num >= m_numNodes) ) {
		return -1;
	}

	XmlNode *node = &m_nodes[num];

	// we can't use hasBackTag() because some tags has back tag but it's not mandatory
	// so we check for void elements
	if ( !node->isTag() || g_nodes[node->getNodeId()].m_tagType == TAG_TYPE_HTML_VOID ) {
		return -1;
	}

	int innerTagCount = 1;

	// scan for ending back tag
	int32_t i;
	for ( i = num + 1 ; i < m_numNodes ; ++i ) {
		if ( m_nodes[i].m_hash == node->m_hash ) {
			if ( m_nodes[i].isFrontTag() ) {
				++innerTagCount;
			} else {
				--innerTagCount;
			}

			if ( innerTagCount == 0 ) {
				break;
			}
		}
	}

	if ( i >= m_numNodes ) {
		return -1;
	}

	return i;
}

int64_t Xml::getCompoundHash ( const char *s , int32_t len ) const {
	// setup
	const char *p     = s;
	const char *start = s;
	int32_t i   = 0;
	int64_t h = 0;
 loop:
	// find fisrt .
	while ( i < len && p[i] != '.' ) i++;
	// . hash from p to p[i]
	// . tag names are always ascii, so use the ascii hasher, not utf8
	h = hash64Upper_a ( start , &p[i] - start , h );
	// bail if done
	if ( i >= len ) return h;
	// then period
	h = hash64 ( "." , 1 , h );
	// skip period
	i++;
	// start now points to next word
	start = &p[i];
	// continue
	goto loop;
}

// . return -1 if not found
// . "tagName" is compound (i.e. "myhouse.myroom" )
int32_t Xml::getNodeNum ( int32_t n0 , int32_t n1 , const char *tagName , int32_t tagNameLen ) const {
	// . since i changed the hash to a zobrist hash, hashing
	//   "dns.ip" is not the same as hashing "dns" then "." then "ip"
	//   by passing the hash of the last to the next as the startHash
	// . therefore, i now parse it up
	int64_t h = getCompoundHash ( tagName , tagNameLen );
	int32_t i;
	if ( n1 > m_numNodes ) n1 = m_numNodes;
	if ( n0 > m_numNodes ) n0 = m_numNodes;
	if ( n1 < 0 ) n1 = 0;
	if ( n0 < 0 ) n0 = 0;
	for ( i = n0 ; i < n1; i++ ) {
		// if node is text (non-tag) then skip
		if ( ! m_nodes[i].isTag() ) continue;
		//if ( m_nodes[i].m_compoundHash == h ) break;
		if ( m_nodes[i].m_hash == h ) break;
	}
	// return -1 if not found at all
	if ( i >= n1 ) return -1;
	return i;
}

void Xml::reset ( ) {
	// free old nodes array if any
	if ( m_nodes ) {
		mfree ( m_nodes, m_maxNumNodes*sizeof(XmlNode),"Xml1");
	}

	m_xml         = NULL;
	m_nodes       = NULL; 
	m_numNodes    = 0;
	m_maxNumNodes = 0;
}


bool Xml::getCompoundName ( int32_t node , SafeBuf *sb ) {
	XmlNode *buf[256];
	XmlNode *xn = &m_nodes[node];
	int32_t np = 0;
	for ( ; xn ; xn = xn->m_parent ) {
		if ( ! xn->m_nodeId ) continue;
		if ( np >= 256 ) {g_errno = EBUFTOOSMALL;return false;}
		buf[np++] = xn;
	}

	// ignore that initial <?xml ..> tag they all have
	if ( np > 0 &&
	     buf[np-1]->m_tagNameLen == 3 &&
	     strncasecmp(buf[np-1]->m_tagName,"xml",3) == 0 )
		np--;

	for ( int32_t i = np - 1 ; i >= 0 ; i-- ) {
		XmlNode *xn = buf[i];
		sb->safeMemcpy ( xn->m_tagName , xn->m_tagNameLen );
		sb->pushChar('.');
	}
	// remove last '.'
	if ( sb->length() ) sb->m_length--;
	sb->nullTerm();
	return true;
}


#include "HttpMime.h" // CT_JSON

// "s" must be in utf8
bool Xml::set( char *s, int32_t slen, int32_t version, int32_t niceness, char contentType ) {
	// just in case
	reset();

	m_version = version;
	m_niceness = niceness;

	// clear it
	g_errno = 0;

	// make pointers to data
	m_xml    = s;
	m_xmlLen = slen;

	// debug msg time
	if ( g_conf.m_logTimingBuild ) {
		logf( LOG_TIMING, "build: xml: set: 4a. %" PRIu64 "", gettimeofdayInMilliseconds() );
	}

	// sanity check
	if ( !s || slen <= 0 ) {
		return true;
	}

	if ( s[slen] != '\0' ) {
		log(LOG_LOGIC,"build: Xml: Content is not null terminated.");
		gbshutdownLogicError();
	}

	// if json go no further. TODO: also do this for CT_TEXT etc.
	if ( contentType == CT_JSON ) {
		m_numNodes = 0;
		// make the array
		m_maxNumNodes = 1;
		m_nodes =(XmlNode *)mmalloc(sizeof(XmlNode)*m_maxNumNodes,"x");
		if ( ! m_nodes ) return false;
		XmlNode *xd = &m_nodes[m_numNodes];
		// hack the node
		xd->m_node       = s;
		xd->m_nodeLen    = slen;
		xd->m_isSelfLink = 0;
		// . nodeId for text nodes is 0
		xd->m_nodeId     = TAG_TEXTNODE;
		xd->m_hasBackTag = false;
		xd->m_hash       = 0;
		xd->m_pairTagNum = -1;
		m_numNodes++;
		return true;
	}

	// override. no don't it hurts when parsing CT_XML docs!!
	// we need XmlNode.cpp's setNodeInfo() to identify xml tags in 
	// an rss feed. No, this was here for XmlDoc::hashXml() i think
	// so let's just fix Links.cpp to get links from pure xml.
	// we can't do this any more. it's easier to fix xmldoc::hashxml()
	// some other way... because Links.cpp and Xml::isRSSFeed() 
	// depend on having regular tagids. but without this here
	// then XmlDoc::hashXml() breaks.
	bool pureXml = ( contentType == CT_XML );

	QUICKPOLL((niceness));
	int32_t i;

	/// @todo ALC why are we replacing NULL bytes here?

	/// Shouldn't all string be valid utf-8 at this point?
	// . replacing NULL bytes with spaces in the buffer
	// . utf8 should never have any 0 bytes in it either!
	for ( i = 0 ; i < slen ; i++ ) {
		if ( !s[i] ) {
			s[i] = ' ';
		}
	}

	// counting the max num nodes
	for ( i = 0 ; s[i] ; i++ ) {
		if ( s[i] == '<' ) {
			m_maxNumNodes++;
		}
	}

	// account for the text (non-tag) nodes (padding nodes between tags)
	m_maxNumNodes *= 2 ;

	// if we only have one tag we can still have 3 nodes!
	m_maxNumNodes++;

	// debug msg time
	if ( g_conf.m_logTimingBuild ) {
		logf( LOG_TIMING, "build: xml: set: 4b. %" PRIu64 "", gettimeofdayInMilliseconds() );
	}

	// . truncate it to avoid spammers
	// . now i limit to 30k nodes because of those damned xls docs!
	// . they have 300,000+ nodes some of 'em

	// now allow 35k nodes for every 100k doclen
	int32_t num100k = slen/(100*1024);
	if (num100k <= 0) num100k = 1;
	int32_t bigMax = 35*1024 * num100k;
	if (m_maxNumNodes > bigMax){
		log(LOG_WARN, "build: xml: doclen %" PRId32", "
		    "too many nodes: counted %" PRId32", max %" PRId32" "
		    "...truncating", slen, m_maxNumNodes, bigMax);
		m_maxNumNodes = bigMax;
	}

	// breathe
	QUICKPOLL ( niceness );

	m_nodes = (XmlNode *)mmalloc( sizeof( XmlNode ) * m_maxNumNodes, "Xml1" );
	if ( ! m_nodes ) { 
		reset();
		return log( "build: Could not allocate %" PRId32 " bytes need to parse document.",
					(int32_t)sizeof( XmlNode ) * m_maxNumNodes );
	}

	// debug msg time
	if ( g_conf.m_logTimingBuild ) {
		logf( LOG_TIMING, "build: xml: set: 4c. %" PRIu64 "", gettimeofdayInMilliseconds() );
	}

	XmlNode *parent = NULL;
	XmlNode *parentStackStart[256];
	XmlNode **parentStackPtr = &parentStackStart[0];
	XmlNode **parentStackEnd = &parentStackStart[256];

	// . TODO: do this on demand
	// . now fill our nodes array
	// . loop over the xml
	// . i is byte-index in buffer
	for ( i = 0 ; i < m_xmlLen && m_numNodes < m_maxNumNodes ; ) {
		// breathe
		QUICKPOLL(niceness);

		// convenience ptr
		XmlNode *xi = &m_nodes[m_numNodes];

		// set that node
		i += xi->set( &m_xml[i], pureXml );

		// set his parent xml node if is xml
		xi->m_parent = parent;

		bool endsInSlash = false;
		if ( xi->m_node[xi->m_nodeLen-2] == '/' ) {
			endsInSlash = true;
		}

		if ( xi->m_node[xi->m_nodeLen-2] == '?' ) {
			endsInSlash = true;
		}

		// disregard </> in the conf files
		if ( xi->m_nodeLen == 3 && endsInSlash ) {
			endsInSlash = false;
		}

		// if not text node then he's the new parent
		// if we don't do this for xhtml then we don't pop the parent
		// and run out of parent stack space very quickly.
		if ( pureXml && xi->m_nodeId &&
		     xi->m_nodeId != TAG_COMMENT && xi->m_nodeId != TAG_CDATA &&
			 !endsInSlash ) {
			// if we are a back tag pop the stack
			if ( ! xi->isFrontTag() ) {
				// pop old parent
				if ( parentStackPtr > parentStackStart ) {
					parent = *(--parentStackPtr);
				}
			}
			// we are a front tag...
			else {
				// did we overflow?
				if ( parentStackPtr >= parentStackEnd ) {
					log("xml: xml parent overflow");
					g_errno = EBUFTOOSMALL;
					return false;
				}

				// push the old parent ptr
				if ( parent ) {
					*parentStackPtr++ = parent;
				}

				// set the new parent to us
				parent = xi;
			}
		}

		if ( xi->m_nodeId != TAG_SCRIPT || !xi->isFrontTag() ) {
			++m_numNodes;
			continue;
		}

		// ok, we got a <script> tag now
		++m_numNodes;

		// use this for parsing consistency when deleting records
		// so they equal what we added.
		bool newVersion = (version > 120);

		//	retry:
		// scan for </script>
		char *pstart = &m_xml[i];
		char *p      = pstart;
		char *pend   = &m_xml[0] + m_xmlLen;
		bool inDoubles = false;
		bool inSingles = false;
		bool inComment1 = false;
		bool inComment2 = false;
		bool inComment3 = false;
		bool inComment4 = false;
		bool escaped    = false;

		// scan -- 5 continues -- node 1570 is text of script
		for ( ; p < pend ; p++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			//
			// adding these new quote checks may cause a few
			// parsing inconsistencies for pages a hanful of pages
			//
			// windows-based html pages use 13 sometimes and no
			// \n at all...
			if ( p[0] =='\n' || p[0] == 13 )  { // ^m = 13 = CR
				//newLine = true;
				inComment1 = false;
			}
			if ( p[0] == '\\' ) {
				escaped = ! escaped;
				continue;
			}
			//if ( newLine && is_wspace_a(p[0]) )
			//	continue;
			if ( p[0] == '<' && p[1] == '!' && 
			     p[2] == '-' && p[3] == '-' &&
			     ! inSingles && ! inDoubles &&
			     ! inComment1 &&
			     ! inComment2 &&
			     ! inComment4 ) 
				inComment3 = true;
			if ( p[0] == '-' && p[1] == '-' && 
			     p[2] == '>' && 
			     inComment3 ) 
				inComment3 = false;
			// no. i saw <script>//</script> and </script> was
			// not considered to be in a comment
			if ( p[0] == '/' && p[1]=='/'&& 
			     ! inSingles && ! inDoubles &&
			     ! inComment2 && 
			     ! inComment3 &&
			     // allow for "//<![CDATA[..." to end in
			     // "//]]>" so ignore if inComment4 is true.
			     // i'd say these are the weaker of all 4 
			     // comment types in that regard.
			     ! inComment4 )
				inComment1 = true;
			// handle /* */ comments
			if ( p[0] == '/' && p[1]=='*' &&
			     ! inSingles && ! inDoubles &&
			     ! inComment1 && 
			     ! inComment3 &&
			     ! inComment4 )
				inComment2 = true;
			// <![CDATA[...]]> "comments" in <script> tags
			// are common. CDATA tags seem to prevail even if
			// within another comment tag, like i am seeing
			// "//<![CDATA[..." a lot.
			if ( p[0] == '<' &&
			     p[1] == '!' &&
			     p[2] == '[' &&
			     p[3] == 'C' &&
			     p[4] == 'D' &&
			     p[5] == 'A' &&
			     p[6] == 'T' &&
			     p[7] == 'A' &&
			     p[8] == '[' 
			     //! inComment1 &&
			     //! inComment2 && 
			     //! inComment3 )
			     )
				inComment4 = true;
			if ( p[0] == ']' &&
			     p[1] == ']' &&
			     p[2] == '>' )
				inComment4 = false;
			if ( p[0] == '*' && 
			     p[1]=='/' &&
			     ! inComment4 )
				inComment2 = false;
			// no longer the start of a newLine
			//newLine = false;
			// don't check for quotes or </script> if in comment
			// no, if've seen <script>//</script> on ibm.com pages,
			// so just ignore ' and " for // comments
			if ( inComment1 && newVersion ) {
				escaped = false;
				//continue;
			}
			if ( inComment2 && newVersion ) {
				escaped = false;
				continue;
			}
			if ( inComment3 && newVersion ) {
				escaped = false;
				continue;
			}
			if ( inComment4 && newVersion ) {
				escaped = false;
				continue;
			}
			// if an unescaped double quote
			if ( p[0] == '\"' && ! escaped && ! inSingles &&
			     // i've seen <script>//</script> on ibm.com pages,
			     // so just ignore ' and " for // comments
			     ! inComment1 ) 
				inDoubles = ! inDoubles;
			// if an unescaped single quote. 
			if ( p[0] == '\'' && ! escaped && ! inDoubles &&
			     // i've seen <script>//</script> on ibm.com pages,
			     // so just ignore ' and " for // comments
			     ! inComment1 ) 
				inSingles = ! inSingles;
			// no longer escaped
			escaped = false;

			// keep going if not a tag
			if ( p[0]  != '<' ) continue;
			// </script> or </gbframe> stops it
			if ( p[1] == '/' ) {
				if ( to_lower_a(p[2]) == 's' &&
				     to_lower_a(p[3]) == 'c' &&
				     to_lower_a(p[4]) == 'r' &&
				     to_lower_a(p[5]) == 'i' &&
				     to_lower_a(p[6]) == 'p' &&
				     to_lower_a(p[7]) == 't' ) {
					if((inDoubles||inSingles)&& newVersion)
						continue;
					break;
				}
				if ( to_lower_a(p[2]) == 'g' &&
				     to_lower_a(p[3]) == 'b' &&
				     to_lower_a(p[4]) == 'f' &&
				     to_lower_a(p[5]) == 'r' &&
				     to_lower_a(p[6]) == 'a' &&
				     to_lower_a(p[7]) == 'm' ) 
					break;
			}
			// another <script> stops it
			if ( to_lower_a(p[1]) == 's' &&
			     to_lower_a(p[2]) == 'c' &&
			     to_lower_a(p[3]) == 'r' &&
			     to_lower_a(p[4]) == 'i' &&
			     to_lower_a(p[5]) == 'p' &&
			     to_lower_a(p[6]) == 't' ) {
				if ( (inDoubles || inSingles) && newVersion )
					continue;
				break;
			}
		}

		// make sure we do not breach! i saw this happen once!
		if ( m_numNodes >= m_maxNumNodes ) break;
		// was it like <script></script> then no scripttext tag?
		if ( p - pstart == 0 )
			continue;

		XmlNode *xn      = &m_nodes[m_numNodes++];
		xn->m_nodeId     = TAG_SCRIPTTEXT;
		xn->m_node       =     pstart;
		xn->m_nodeLen    = p - pstart;
		xn->m_tagName    = NULL;
		xn->m_tagNameLen = 0;
		xn->m_hasBackTag = false;
		xn->m_hash       = 0;
		xn->m_isVisible  = false;
		xn->m_isBreaking = false;
		// advance i to get to the </script> or <gbframe> etc.
		i = p - &m_xml[0] ;
	}

	// sanity
	if ( m_numNodes > m_maxNumNodes ) gbshutdownLogicError();

	// trim off last node if empty! it is causing a core in isBackTag()
	if ( m_numNodes > 0 && m_nodes[m_numNodes-1].m_nodeLen == 0 ) {
		m_numNodes--;
	}

	// debug msg time
	if ( g_conf.m_logTimingBuild ) {
		logf( LOG_TIMING, "build: xml: set: 4d. %" PRIu64 "", gettimeofdayInMilliseconds() );
	}

	return true;
}

// for translating HTML entities to an iso char
#include "Entities.h"

// . replaces line-breaking html tags with 2 returns if "includeTags" is false
// . stores tags too if "includeTags" is true
// . returns # chars written to buf
// . NOTE: see XmlNode.cpp for list of tag types in "NodeType" structure
// . used to get xml subtrees as text
// . used to get <TITLE>'s
// . must write to your buf rather than just return a pointer since we may
//   have to concatenate several nodes together, we may have to replace tags,..
// . TODO: nuke this in favor of Pos.cpp::filter() -- but that needs Words.cpp
int32_t Xml::getText( char *buf, int32_t bufMaxSize, int32_t node1, int32_t node2, bool filterSpaces ) {
	// init some vars
	int32_t i    = node1;
	int32_t n    = node2;

	// truncate n to the # of nodes we have
	if ( n > m_numNodes ) n = m_numNodes;

	// keep a non visible tag stack
	int32_t notVisible = 0;

	// the destination
	char *dst    = buf;
	char *dstEnd = buf + bufMaxSize;

	char cs = -1;

	// loop through all nodes from here on until we run outta nodes...
	// or until we hit a tag with the same depth as us.
	for ( ; i < n ; i++ ) {
		// . set skipText to true if this tag has inivisble text
		// . examples: <option> <script> ...
		if ( m_nodes[i].isTag() && ! m_nodes[i].isVisible() &&
		     m_nodes[i].hasBackTag() ) {
			if ( m_nodes[i].isFrontTag() ) notVisible++;
			else                           notVisible--;
			if ( notVisible < 0 ) notVisible = 0;
		}

		// . if it's a tag then write a \n\n or \n to the buf
		// . do this only if we do not include tags
		// . do it only if there's something already in the buf
		if ( m_nodes[i].isTag() ) {
			// do nothing if buf still empty
			if ( dst <= buf ) continue;
			// or not a breaking tag
			if ( ! m_nodes[i].isBreaking() ) continue;
			// forgot this check! leave room for terminating \0
			if ( dst + 3 >= dstEnd ) break;
			// if we're not junk filtering just add 2 \n's
			if ( ! filterSpaces ) {
				*dst++='\n';
				*dst++='\n';
				continue;
			}

			// need at least 2 chars in the dst buf so far
			if ( dst - 1 <= buf           ) continue;
			if ( cs == -1                 ) continue;
			// . if prev char is punct, do nothing.
			// . check prev prev char to make sure not a single chr
			// . TODO: fix this!
			if ( is_punct_a( *(dst - cs))) continue;
			//if ( is_punct_utf8( dst[-1])  ) continue;
			if ( i+1 >= n                 ) continue;
			if ( is_punct_utf8 ( &m_nodes[i+1].m_node[0] ) 
			     && !m_nodes[i+1].isTag() ) continue;
			// . watch out for punct before space(s) though
			// . it also ensures that this char is the first char
			//   of any potential multi-byte sequence
			if ( is_wspace_utf8 ( dst - cs ) ) {
				// back up one before that even
				char *f = dst - cs - 1;
				// don't do a while loop on this 
				// cuz with those xls docs we can 
				// have a TON of spaces cuz their 
				// just a bunch of <td></td>&nbsp;'s
				if ( f > buf && is_wspace_a ( *f ) ) f--;
				if ( f > buf && is_wspace_a ( *f ) ) f--;
				if ( f > buf && is_wspace_a ( *f ) ) f--;
				if ( f > buf && is_ascii(*f)&&is_punct_a(*f) )
					continue;
			}
			// ok, add the ".."
			*dst++='.';
			*dst++='.';
			continue;
		}

		// if this tag/text is not visible then continue
		if ( notVisible ) continue; 

		// . get a ptr to the node's data
		// . is 1 of 3 things: a text blob, xml tag or html tag
		char *nodeData    = m_nodes[i].getNode   ();
		int   nodeDataLen = m_nodes[i].getNodeLen();

		// . truncate the node if it's too big
		// . make sure we truncate at a non alphanumeric character
		// . avoid breaking in the middle of a word
		// . we cannot truncate tags
		if ( dst + nodeDataLen  >= dstEnd ) { // bufMaxSize ) {
			// cannot truncate tags
			if ( m_nodes[i].isTag() ) break;
			nodeDataLen = dstEnd - dst - 2;//bufMaxSize - blen;
			while ( nodeDataLen > 0  && 
				! is_wspace_a(nodeData[ nodeDataLen-1 ])) 
				nodeDataLen--;
		}
		
		// if we truncated the whole thing just break out, we're done.
		if ( nodeDataLen <= 0 ) break;

		// . copy the node data into our buffer
		// . translate HTML entities to iso characters
		// . translate \r's into spaces

		// point to it
		char *src    = nodeData;
		char *srcEnd = nodeData + nodeDataLen;

		// copy the node @src into "dst"
		for ( ; src < srcEnd ; src += cs , dst += cs ) {
			// get the character size in bytes
			cs = getUtf8CharSize ( src );
			// no back to back spaces if we're filtering junk
			if ( filterSpaces && is_wspace_utf8 ( src ) ) {
				if ( dst     <= buf ) {dst -= cs; continue;}
				if ( dst[-1] == ' ' ) {dst -= cs; continue;}
				// ok, do not filter it
				//goto simplecopy;
			}

			// if more than 1 byte in char, use gbmemcpy
			if ( cs > 1 ) {gbmemcpy ( dst , src , cs );}
			else          *dst = *src;
		}
		// continue looping over nodes (text and tag nodes)
	}

	// . strip trailing spaces
	// . is_wspace_utf8 will be false if it is not the first character
	//   of a utf8 char sequence, and i don't count any multi-byte
	//   spaces i guess...
	while ( dst > buf && is_wspace_a ( dst[-1] ) ) dst--;

	// null term it
	*dst = '\0';

	// return the # of bytes we've written into the buffer.
	return dst - buf;
}

// just get a pointer to it
char *Xml::getMetaContentPointer( const char *field, int32_t fieldLen, const char *name, int32_t *slen ) {
	// find the first meta summary node
	for ( int32_t i = 0 ; i < m_numNodes ; i++ ) {
		// continue if not a meta tag
		if ( m_nodes[i].m_nodeId != TAG_META ) continue;
		// . does it have a type field that's "summary"
		// . <meta name=summary content="...">
		// . <meta http-equiv="refresh" content="0;URL=http://y.com/">
		int32_t len;
		char *s = getString ( i , name , &len );
		// continue if name doesn't match field
		if ( len != fieldLen ) continue;
		// field can be "summary","description","keywords",...
		if ( strncasecmp ( s , field , fieldLen ) != 0 ) continue;
		// point to the summary itself
		s = getString ( i , "content" , &len );
		if ( ! s || len <= 0 ) continue;
		// return the pointer (and set the length of what it points to)
		*slen = len;
		return s;
	}
	*slen = 0;
	return NULL;
}

// . extract the content from a meta tag
// . null terminate it and store it into "buf"
// . field can be stuff like "summary","description","keywords",...
// . TODO: have a filter option to filter out back-to-back spaces for summary
//         generation purposes in Summary class
// . "name" is usually "name" or "http-equiv"
// . if "convertHtmlEntities" is true we turn < into &lt; and > in &gt;
int32_t Xml::getMetaContent( char *buf, int32_t bufLen, const char *field, int32_t fieldLen, const char *name,
							 int32_t startNode, int32_t *matchedNode ) {
	// return 0 length if no buffer space
	if ( bufLen <= 0 ) return 0;
	// assume it's empty
	buf[0] = '\0';
	// assume no tag matched
	if ( matchedNode ) *matchedNode = -1;
	// store output into "dst"
	char *dst    = buf;
	char *dstEnd = buf + bufLen;
	// find the first meta summary node
	for ( int32_t i = startNode ; i < m_numNodes ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);

		// continue if not a meta tag
		if ( m_nodes[i].m_nodeId != TAG_META ) {
			continue;
		}

		// . does it have a type field that's "summary"
		// . <meta name=summary content="...">
		// . <meta http-equiv="refresh" content="0;URL=http://y.com/">
		int32_t len;
		char *s = getString ( i , name , &len );

		// continue if name doesn't match field
		// field can be "summary","description","keywords",...
		if ( len != fieldLen ) {
			continue;
		}

		if ( strncasecmp ( s , field , fieldLen ) != 0 ) {
			continue;
		}

		// point to the summary itself
		s = getString ( i , "content" , &len );
		if ( ! s || len <= 0 ) {
			continue;
		}

		// point to it
		char *src    = s;
		char *srcEnd = s + len;
		// size of character in bytes, usually 1
		char cs ;
		// bookmark
		char *lastp = NULL;
		// copy the node @p into "dst"
		for ( ; src < srcEnd ; src+= cs ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get the character size in bytes
			cs = getUtf8CharSize ( src );

			// break if we are full! (save room for \0)
			if ( dst + 5 >= dstEnd ) break;

			// remember last punct for cutting purposes
			if ( ! is_alnum_utf8 ( src ) ) lastp = dst;

			// if more than 1 byte in char, use gbmemcpy
			if ( cs > 1 ) {gbmemcpy ( dst , src , cs );}
			else          *dst = *src;
			dst += cs;
		}

		// continue looping over nodes (text and tag nodes)

		// do not split a word in the middle! so if we had to
		// truncate, at least try to truncate at last punctuation
		// mark if we had one.
		if ( dst + 5 >= dstEnd && lastp ) {
			*lastp = '\0';
			len = lastp - buf;
		}
		// end at dst as well
		else {
			*dst = '\0';
			len = dst - buf;
		}

		// store node number
		if ( matchedNode ) {
			*matchedNode = i;
		}

		return len;
	}
	return 0;
}

static bool inTag ( XmlNode *node, nodeid_t tagId, int *count ) {
	if ( !count ) {
		return false;
	}

	if ( node->getNodeId() == tagId ) {
		if ( node->isFrontTag() ) {
			++(*count);
			return true;
		}

		// back tag
		if ( *count ) {
			--(*count);
		}
	}

	return (*count > 0);
}

static int32_t filterContent( Words *wp, Pos *pp, char *buf, int32_t bufLen, int32_t minLength,
							  int32_t maxLength, int32_t version ) {
	int32_t contentLen = 0;

	/// @todo ALC configurable maxNumWord so we can tweak this as needed
	const int32_t maxNumWord = (maxLength * 2);

	if ( wp->getNumWords() > maxNumWord ) {
		// ignore too long snippet
		// it may not be that useful to get the first x characters from a long snippet
		contentLen = 0;
		buf[0] = '\0';

		return contentLen;
	}

	contentLen = pp->filter( wp, 0, wp->getNumWords(), true, buf, buf + maxLength, version );

	if ( contentLen < minLength ) {
		// ignore too short descriptions
		// it may not be a good summary if it's too short
		contentLen = 0;
		buf[0] = '\0';

		return contentLen;
	}
	return contentLen;
}

bool Xml::getTagContent( const char *fieldName, const char *fieldContent, char *buf, int32_t bufLen,
						 int32_t minLength, int32_t maxLength, int32_t *contentLenPtr,
						 bool ignoreExpandedIframe, nodeid_t expectedNodeId ) {
	int32_t fieldNameLen = strlen( fieldName );
	int32_t fieldContentLen = strlen(fieldContent);
	int32_t contentLen = 0;
	int inTagCount = 0;

	for (int32_t i = 0; i < getNumNodes(); ++i ) {
		// don't get tag from gbframe (expanded iframe content)
		if ( ignoreExpandedIframe && inTag( getNodePtr( i ), TAG_GBFRAME, &inTagCount ) ) {
			continue;
		}

		if ( expectedNodeId != LAST_TAG && getNodeId(i) != expectedNodeId ) {
			continue;
		}

		bool found = false;
		if ( fieldNameLen > 0 ) {
			int32_t tagLen = 0;
			char *tag = getNodePtr(i)->getAttrValue(fieldName, fieldNameLen, &tagLen);
			if ( tagLen == fieldContentLen && strncasecmp( tag, fieldContent, fieldContentLen ) == 0 ) {
				found = true;
			}
		} else {
			found = true;
		}

		if ( found ) {
			int32_t end_node = getEndNode(i);

			Words wp;
			Pos pp;

			if (end_node < 0) {
				if ( getNodeId(i) != TAG_META ) {
					// no end tag
					continue;
				}

				// extract content from meta tag
				int32_t len = 0;
				char *s = getNodePtr(i)->getAttrValue("content", 7, &len);
				if ( ! s || len <= 0 ) {
					// no content
					continue;
				}

				Xml xml;
				{
					/// @todo ALC workaround until we fix xml to use len instead of '\0'
					char saved = s[len];
					s[len] = '\0';

					if ( !xml.set( s, len, m_version, 0, CT_HTML ) ) {
						s[len] = saved;
						return false;
					}

					s[len] = saved;
				}

				if ( ( !wp.set(&xml, true) ) ) {
					// unable to allocate buffer
					return false;
				}
			} else {
				if ( !wp.set(this, true, 0, i, end_node ) ) {
					// unable to allocate buffer
					return false;
				}
			}

			if ( !pp.set( &wp ) ) {
				// unable to allocate buffer
				return false;
			}

			contentLen = filterContent( &wp, &pp, buf, bufLen, minLength, maxLength, m_version );
			if ( contentLen > 0 ) {
				if (contentLenPtr) {
					*contentLenPtr = contentLen;
				}

				/// @todo ALC we may want to loop through the whole doc and get the best.
				/// Only get the first for now
				break;
			}
		}
	}

	return (contentLen > 0);
}

//  TEST CASES:
//. this is NOT rss, but has an rdf:rdf tag in it!
//  http://www.silverstripe.com/silverstripe-adds-a-touch-of-design-and-a-whole-lot-more/
//  http://government.zdnet.com/?p=4245
int32_t Xml::isRSSFeed ( ) {
	int32_t type = 0;
	int32_t tag  = 0;
	int32_t i;
	for ( i = 0; i < m_numNodes; i++ ) {
		// skip text nodes (nodeId is 0)
		if ( m_nodes[i].m_nodeId == TAG_TEXTNODE ) continue;
		// check for RSS/FEED/RDF node
		if ( m_nodes[i].m_nodeId == TAG_RDF  ) { 
			tag = TAG_RDF; type = 1; }
		if ( m_nodes[i].m_nodeId == TAG_RSS  ) {
			tag = TAG_RSS; type = 1; }
		if ( m_nodes[i].m_nodeId == TAG_FEED ) {
			tag = TAG_FEED; type = 6; }
		if ( tag ) break;
	}
	// if no such tag we are definitely not rss
	if ( ! tag ) return 0;
	// i have only seen rdf tags embedded in html
	if ( tag != TAG_RDF ) return type;
	// . now check for a <channel>, <item> or <link> tag
	// . we need one of those to be useful
	for ( i = 0; i < m_numNodes; i++ ) {
		if ( m_nodes[i].m_nodeId == TAG_CHANNEL ) return type;
		if ( m_nodes[i].m_nodeId == TAG_ITEM    ) return type;
		if ( m_nodes[i].m_nodeId == TAG_ENTRY   ) return type;
		//if ( m_nodes[i].m_nodeId == TAG_LINK    ) return type;
	}
	return 0;
}

char *Xml::getRSSTitle ( int32_t *titleLen , bool *isHtmlEncoded ) {
	// assume it is html encoded (i.e. <'s are encoded as &lt;'s)
	*isHtmlEncoded = true;
	// . extract the RSS/Atom title
	// rss/rdf
	int32_t tLen;

	char *title = getString( "title", &tLen, true );

	// watch out for <![CDATA[]]> block
	if ( tLen >= 12 && strncasecmp(title, "<![CDATA[", 9) == 0 ) {
		title += 9;
		tLen  -= 12;
		*isHtmlEncoded = false;
	}

	// return
	*titleLen  = tLen;
	return title;
}

const char *Xml::getRSSTitle ( int32_t *titleLen , bool *isHtmlEncoded ) const {
	return const_cast<Xml*>(this)->getRSSTitle(titleLen,isHtmlEncoded);
}

char *Xml::getRSSDescription ( int32_t *descLen , bool *isHtmlEncoded ) {
	// assume it is html encoded (i.e. <'s are encoded as &lt;'s)
	*isHtmlEncoded = true;
	// . extract the RSS/Atom description
	// rss/rdf
	int32_t dLen;

	// "item.description"
	char *desc = getString( "description", &dLen, true );

	// get content first, it is usually more inclusive than the summary
	if ( ! desc ) {
		// "entry.content"
		desc = getString( "content", &dLen, true );
	}
	// atom
	if ( ! desc ) {
		// "entry.summary"
		desc = getString( "summary", &dLen, true );
	}

	// watch out for <![CDATA[]]> block
	if ( dLen >= 12 && strncasecmp(desc, "<![CDATA[", 9) == 0 ) {
		desc += 9;
		dLen -= 12;
		*isHtmlEncoded = false;
	}

	// return
	*descLen = dLen;
	return desc;
}
