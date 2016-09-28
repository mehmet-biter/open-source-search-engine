#include "gb-include.h"

#include "XmlNode.h"
#include "Mem.h"
#include "Sanity.h"


// . Here's a nice list of all the html nodes names, lengths, whether they're
//   a breaking node or not and their node id
// . isVisible is true if text in between front and end tags is visible on page
// . isVisible is used by Xml::getText() 
// . filterKeep is 1 if we should keep it when &strip=1 is given when getting
//   the cached document. i added this for faisal
// . a filterKeep of 0 means remove tag and text between it and its back tag.
// . a filterKeep of 1 means keep the tag and text between it and its back tag.
// . a filterKeep of 2 means remove tag BUT keep the text between
//   it and its back tag. 
NodeType g_nodes[] = {
    // NAME hasBackTag brk? isVisible? filterKeep1? filterKeep2 type/m_nodeId[i] isXml?
    // --------------------------
    //  -- text node    ---  0
	{"textNode" , 0, 0, 1, 1,1, TAG_TEXTNODE  , 0},
	//  -- xml tag node ---  1
	{"xmlTag"   , 1, 1, 1, 2,2, TAG_XMLTAG    , 0},
	{"A"        , 1, 0, 1, 1,1, TAG_A         , 0},
	{"ABBREV"   , 1, 1, 1, 2,2, TAG_ABBREV    , 0},
	{"ACRONYM"  , 1, 1, 1, 2,1, TAG_ACRONYM   , 0},
	{"ADDRESS"  , 1, 1, 1, 2,2, TAG_ADDRESS   , 0},
	{"APPLET"   , 1, 1, 1, 0,0, TAG_APPLET    , 0},
	{"AREA"     , 0, 1, 1, 0,0, TAG_AREA      , TAG_TYPE_HTML_VOID},
	{"AU"       , 1, 1, 1, 0,0, TAG_AU        , 0},
	{"AUTHOR"   , 1, 1, 1, 0,0, TAG_AUTHOR    , 0},
	{"B"        , 1, 0, 1, 1,1, TAG_B         , 0},
	{"BANNER"   , 1, 1, 1, 0,0, TAG_BANNER    , 0},
	{"BASE"     , 0, 1, 1, 0,0, TAG_BASE      , TAG_TYPE_HTML_VOID},
	{"BASEFONT" , 0, 1, 1, 2,2, TAG_BASEFONT  , 0},
	{"BGSOUND"  , 0, 1, 1, 0,0, TAG_BGSOUND   , 0},
	{"BIG"      , 1, 0, 1, 2,1, TAG_BIG       , 0},
	{"BLINK"    , 1, 0, 1, 2,2, TAG_BLINK     , 0},
	{"BLOCKQUOTE",1, 1, 1, 2,1, TAG_BLOCKQUOTE, 0},
	{"BQ"       , 1, 1, 1, 0,0, TAG_BQ        , 0},
	{"BODY"     , 1, 1, 1, 1,1, TAG_BODY      , 0},
	{"BR"       , 0, 1, 1, 1,1, TAG_BR        , TAG_TYPE_HTML_VOID},
	{"CAPTION"  , 1, 1, 1, 2,1, TAG_CAPTION   , 0},
	{"CENTER"   , 1, 1, 1, 1,1, TAG_CENTER    , 0},
	{"CITE"     , 1, 1, 1, 2,1, TAG_CITE      , 0},
	{"CODE"     , 1, 1, 1, 2,1, TAG_CODE      , 0},
	{"COL"      , 1, 1, 1, 2,2, TAG_COL       , TAG_TYPE_HTML_VOID},
	{"COLGROUP" , 1, 1, 1, 0,0, TAG_COLGROUP  , 0},
	{"CREDIT"   , 1, 1, 1, 0,0, TAG_CREDIT    , 0},
	{"DEL"      , 1, 1, 1, 2,1, TAG_DEL       , 0},
	{"DFN"      , 1, 1, 1, 2,1, TAG_DFN       , 0},
	{"DIR"      , 1, 1, 1, 0,0, TAG_DIR       , 0},
	{"DIV"      , 1, 1, 1, 1,1, TAG_DIV       , 0},
	{"DL"       , 1, 1, 1, 1,1, TAG_DL        , 0},
	// this may not have a back tag!
	{"DT"       , 1, 1, 1, 1,1, TAG_DT        , 0},
	// this may not have a back tag!
	{"DD"       , 1, 1, 1, 1,1, TAG_DD        , 0},
	{"EM"       , 1, 0, 1, 2,1, TAG_EM        , 0}, // emphasized text
	{"EMBED"    , 0, 1, 1, 0,0, TAG_EMBED     , TAG_TYPE_HTML_VOID},
	{"FIG"      , 1, 1, 1, 0,0, TAG_FIG       , 0},
	{"FN"       , 1, 1, 1, 0,0, TAG_FN        , 0},
	{"FONT"     , 1, 0, 1, 1,1, TAG_FONT      , 0},
	{"FORM"     , 1, 1, 1, 2,2, TAG_FORM      , 0},
	// this may not have a back tag!
	{"FRAME"    , 1, 1, 1, 0,0, TAG_FRAME     , 0},
	{"FRAMESET" , 1, 1, 1, 0,0, TAG_FRAMESET  , 0},
	{"H1"       , 1, 1, 1, 1,1, TAG_H1        , 0},
	{"H2"       , 1, 1, 1, 1,1, TAG_H2        , 0},
	{"H3"       , 1, 1, 1, 1,1, TAG_H3        , 0},
	{"H4"       , 1, 1, 1, 1,1, TAG_H4        , 0},
	{"H5"       , 1, 1, 1, 1,1, TAG_H5        , 0},
	{"H6"       , 1, 1, 1, 1,1, TAG_H6        , 0},
	{"HEAD"     , 1, 1, 1, 1,1, TAG_HEAD      , 0},
	{"HR"       , 0, 1, 1, 1,1, TAG_HR        , TAG_TYPE_HTML_VOID},
	{"HTML"     , 1, 1, 1, 1,1, TAG_HTML      , 0},
	{"I"        , 1, 0, 1, 2,1, TAG_I         , 0},
	{"IFRAME"   , 1, 1, 1, 2,2, TAG_IFRAME    , 0},
	// filter = 1,but tag is turned to alt
	{"IMG"      , 0, 1, 1, 1,1, TAG_IMG       , TAG_TYPE_HTML_VOID},
	{"INPUT"    , 0, 1, 1, 0,0, TAG_INPUT     , TAG_TYPE_HTML_VOID},
	{"INS"      , 1, 1, 1, 2,1, TAG_INS       , 0},
	{"ISINDEX"  , 0, 1, 1, 0,0, TAG_ISINDEX   , 0},
	{"KBD"      , 1, 1, 1, 2,1, TAG_KBD       , 0},
	{"LANG"     , 1, 1, 1, 0,0, TAG_LANG      , 0},
	{"LH"       , 1, 1, 1, 0,0, TAG_LH        , 0},
	// this may or may not have a back tag
	{"LI"       , 1, 1, 1, 1,1, TAG_LI        , 0},
	// this may or may not have a back tag
	{"LINK"     , 0, 1, 1, 0,0, TAG_LINK      , TAG_TYPE_HTML_VOID},
	{"LISTING"  , 1, 1, 1, 0,0, TAG_LISTING   , 0},
	{"MAP"      , 1, 1, 1, 0,0, TAG_MAP       , 0},
	// don't index marquee text
	{"MARQUEE"  , 1, 1, 0, 2,2, TAG_MARQUEE   , 0},
	{"MATH"     , 1, 1, 1, 0,0, TAG_MATH      , 0},
	{"MENU"     , 1, 1, 1, 1,1, TAG_MENU      , 0},
    // TAG_MENUITEM (TAG_TYPE_HTML_VOID)
	{"META"     , 0, 1, 1, 1,1, TAG_META      , TAG_TYPE_HTML_VOID},
	{"MULTICOL" , 0, 1, 1, 0,0, TAG_MULTICOL  , 0},
	{"NOBR"     , 1, 0, 1, 0,0, TAG_NOBR      , 0},
	{"NOFRAMES" , 1, 1, 1, 0,0, TAG_NOFRAMES  , 0},
	{"NOTE"     , 1, 1, 1, 0,0, TAG_NOTE      , 0},
	{"OL"       , 1, 1, 1, 1,1, TAG_OL        , 0},
	{"OVERLAY"  , 0, 1, 1, 0,0, TAG_OVERLAY   , 0},
	// this may not have a back tag!
	{"P"        , 0, 1, 1, 1,1, TAG_P         , 0},
	{"PARAM"    , 0, 1, 1, 0,0, TAG_PARAM     , TAG_TYPE_HTML_VOID},
	{"PERSON"   , 1, 1, 1, 0,0, TAG_PERSON    , 0},
	{"PLAINTEXT", 1, 1, 1, 0,0, TAG_PLAINTEXT , 0},
	{"PRE"      , 1, 1, 1, 2,1, TAG_PRE       , 0},
	{"Q"        , 1, 1, 1, 2,1, TAG_Q         , 0},
	{"RANGE"    , 0, 1, 1, 0,0, TAG_RANGE     , 0},
	{"SAMP"     , 1, 1, 1, 2,1, TAG_SAMP      , 0},
	{"SCRIPT"   , 1, 1, 0, 0,0, TAG_SCRIPT    , 0},
	{"SELECT"   , 1, 1, 0, 0,0, TAG_SELECT    , 0},
	{"SMALL"    , 1, 0, 1, 2,1, TAG_SMALL     , 0},
    // TAG_SOURCE (TAG_TYPE_HTML_VOID)
	{"SPACER"   , 0, 1, 1, 2,1, TAG_SPACER    , 0},
	{"SPOT"     , 0, 1, 1, 0,0, TAG_SPOT      , 0},
	{"STRIKE"   , 1, 1, 1, 2,1, TAG_STRIKE    , 0},
	{"STRONG"   , 1, 0, 1, 2,1, TAG_STRONG    , 0},
	{"SUB"      , 1, 0, 1, 2,2, TAG_SUB       , 0},
	{"SUP"      , 1, 0, 1, 2,2, TAG_SUP       , 0},
	{"TAB"      , 0, 1, 1, 0,0, TAG_TAB       , 0},
	{"TABLE"    , 1, 1, 1, 1,1, TAG_TABLE     , 0},
	{"TBODY"    , 1, 1, 1, 1,1, TAG_TBODY     , 0},

	// this may not have a back tag!
	{"TD"       , 1, 1, 1, 1,1, TAG_TD        , 0},
	{"TEXTAREA" , 1, 1, 1, 2,2, TAG_TEXTAREA  , 0},
	{"TEXTFLOW" , 0, 1, 1, 0,0, TAG_TEXTFLOW  , 0},
	{"TFOOT"    , 0, 1, 1, 0,0, TAG_TFOOT     , 0},
	// this DOES have a back tag
	{"TH"       , 1, 1, 1, 0,0, TAG_TH        , 0},
	{"THEAD"    , 0, 1, 1, 0,0, TAG_THEAD     , 0},
	{"TITLE"    , 1, 1, 1, 1,1, TAG_TITLE     , 0},

	// this may not have a back tag!
	{"TR"       , 1, 1, 1, 1,1, TAG_TR        , 0},
    // TAG_TRACK (TAG_TYPE_HTML_VOID)
	{"TT"       , 1, 1, 1, 2,1, TAG_TT        , 0},

	{"U"        , 1, 0, 1, 1,1, TAG_U         , 0},
	{"UL"       , 1, 0, 1, 1,1, TAG_UL        , 0},
	{"VAR"      , 1, 1, 1, 2,1, TAG_VAR       , 0},
	{"WBR"      , 0, 1, 1, 0,0, TAG_WBR       , TAG_TYPE_HTML_VOID},
	{"XMP"      , 1, 1, 1, 0,0, TAG_XMP       , 0},
	{"!--"      , 0, 1, 1, 0,0, TAG_COMMENT   , 0}, // comment tag!

	{"OPTION"   , 0, 1, 1, 2,2, TAG_OPTION    , 0},
	{"STYLE"    , 1, 1, 0, 0,1, TAG_STYLE     , 0},

	// doctype tag <!DOCTYPE ...>
	{"DOCTYPE"  , 0, 1, 1, 0,0, TAG_DOCTYPE   , 0},

	// used in office.microsoft.com <?xml ...>
	{"XML"      , 0, 1, 1, 0,0, TAG_XML       , 0},

	// <start index> <stop index>
	{"START"    , 0, 1, 1, 0,0, TAG_START     , 0},
	{"STOP"     , 0, 1, 1, 0,0, TAG_STOP      , 0},

	// . i added these tags for faisal, but don't really need them
	//   since our XML tag condition handles this case
	// . we can no longer treat as a generic XML tags since faisal wanted
	//   the strip=2 option
	{"SPAN"     , 1, 0, 1, 2,1, TAG_SPAN      , 0}, // not breaking!
	{"LEGEND"   , 1, 1, 1, 2,1, TAG_LEGEND    , 0},
	{"S"        , 1, 1, 1, 2,1, TAG_S         , 0}, // strike tag

	{"ABBR"     , 1, 0, 1, 2,1, TAG_ABBR      , 0},
	{"![CDATA[" , 0, 1, 1, 0,0, TAG_CDATA     , 0}, // <![CDATA[ tag
	{"NOSCRIPT" , 1, 1, 0, 0,0, TAG_NOSCRIPT  , 0},
    {"FIELDSET" , 1, 1, 1, 0,0, TAG_FIELDSET  , 0},

	// feedburner uses these in the xml
	{"FEEDBURNER:ORIGLINK", 0, 1, 1, 0,0, TAG_FBORIGLINK , TAG_TYPE_XML},

	// ahrefs uses these as links
	{"RDF:RDF"  , 0, 1, 1, 0,0, TAG_RDF       , TAG_TYPE_XML},
    {"RSS"      , 0, 1, 1, 0,0, TAG_RSS       , TAG_TYPE_XML},
	{"FEED"     , 0, 1, 1, 0,0, TAG_FEED      , TAG_TYPE_XML},

	{"ITEM"     , 1, 1, 0, 0,0, TAG_ITEM      , TAG_TYPE_XML},
	{"ENTRY"    , 1, 1, 0, 0,0, TAG_ENTRY     , TAG_TYPE_XML},
	{"CHANNEL"  , 1, 1, 0, 0,0, TAG_CHANNEL   , TAG_TYPE_XML},
	{"ENCLOSURE", 1, 1, 0, 0,0, TAG_ENCLOSURE , 0},
	{"WEBLOG"   , 0, 1, 0, 0,0, TAG_WEBLOG    , TAG_TYPE_XML},

	{"GBFRAME"  , 1, 1, 1, 1,1, TAG_GBFRAME   , 0},
	{"TC"       , 1, 1, 1, 1,1, TAG_TC        , 0},// HACK: tbl column section
	{"GBXMLTITLE", 1, 1, 1, 1,1, TAG_GBXMLTITLE, TAG_TYPE_XML},

	// facebook xml
	{"START_TIME", 1, 1, 1, 1,1, TAG_FBSTARTTIME, TAG_TYPE_XML},
	{"END_TIME" , 1, 1, 1, 1,1, TAG_FBENDTIME, TAG_TYPE_XML},
	{"NAME"     , 1, 1, 1, 1,1, TAG_FBNAME, TAG_TYPE_XML},
	{"PIC_SQUARE", 1, 1, 1, 1,1, TAG_FBPICSQUARE, TAG_TYPE_XML},
	{"HIDE_GUEST_LIST", 1, 1, 1, 1,1, TAG_FBHIDEGUESTLIST, TAG_TYPE_XML},


	{"scriptText",0, 1, 0, 0,0, TAG_SCRIPTTEXT,0 },
	{"BUTTON"   , 1, 1, 1, 0,0, TAG_BUTTON,0},
	{"UrlFrom"  , 0, 1, 1, 0,0, TAG_URLFROM, TAG_TYPE_XML},

	// for sitemap.xml
	{"LOC"      , 0, 1, 1, 0,0, TAG_LOC, 0}
};
// NAME hasBackTag brk? isVisible? filterKeep1? filterKeep2 type/m_nodeId[i]


// . called by Xml class
// . returns the length of the node
// . TODO: "node" is now guaranteed to be \0 terminated -- make this faster
int32_t XmlNode::set( char *node, bool pureXml ) {
	// save head of node
	m_node = node;

	// sanity check
	static bool s_check = false;
	if ( ! s_check ) {
		s_check = true;

		// how many NodeTypes do we have in g_nodes?
		static const int32_t nn = sizeof(g_nodes) / sizeof(NodeType);

		// set the hash table
		for ( int32_t i = 0 ; i < nn ; i++ ) {
			// sanity
			if ( g_nodes[i].m_nodeId != i ) gbshutdownLogicError();
		}
	}

	// . reset this
	// . need to do here instead of in Links.cpp because sometimes
	//   we think an anchor tag indicates a link, but it is really
	//   just an <a href="javascript:..."> function call and Links.cpp
	//   ignored it but we are expecting this to be valid!
	m_isSelfLink = 0;

	// CDATA tag was identified in earlier versions as a text node. Now 
	// it is identified as a CDATA tag node. But gb.conf and others always
	// pass their version as 0
	if ( node[0] == '<' &&
	     node[1] == '!' &&
	     node[2] == '[' &&
	     node[3] == 'C' &&
	     node[4] == 'D' &&
	     node[5] == 'A' &&
	     node[6] == 'T' &&
	     node[7] == 'A' &&
	     node[8] == '[' ) {
		return setCDATANode ( node );
	}

	// if "node" isn't the start of a tag then set it as a Text Node
	if ( ! isTagStart ( node ) ) {
		// . set this node as a text node!
		// . nodeId for text nodes is 0
		m_nodeId     = TAG_TEXTNODE;
		m_node       = node;
		m_hasBackTag = false;
		m_hash       = 0;
		int32_t i = 0;

		// inc i as int32_t as it's NOT the beginning of a tag
		while ( node[i] && ( node[i] != '<' || !isTagStart( node + i ) ) ) {
			++i;
		}

		m_nodeLen = i;
		m_pairTagNum = -1;

		return m_nodeLen;
	}

	// . see if it's a comment (node end is "-->" for comments)
	// . comments are special cases
	if  ( node[1] == '!' ) {
		if ( node[2]=='-' && node[3]=='-' ) {
			return setCommentNode ( node );
		}

		// this means comment too:
		// <![if ....]>
		if ( node[2]=='[' ) {
			return setCommentNode2 ( node );
		}
	}

	// . otherwise it's a regular tag
	// . might be <!DOCTYPE ...> or something though
	m_nodeLen = getTagLen ( node );

	// . get the node's name's length (i-1)
	// . node name ends at non alnum char 
	// . we can have hyphens in node name (TODO: even at beginning???)
	int32_t tagNameStart = 1;

	// . skip over backslash in the back tags
	// . or skip over / or ? or ! now
	// . tag names must start with a letter, fwiw
	if ( ! is_alnum_a(node[tagNameStart]) ) {
		tagNameStart++;
	}

	int32_t i = tagNameStart;

	// skip i to end of tagName. this should only allow ascii chars
	// to be "tag name chars"
	for ( ; i < m_nodeLen && is_tagname_char(node[i]) ; i++ );

	// set the tagName and tagNameLen
	m_tagName    = &node [ tagNameStart ];
	m_tagNameLen = i - tagNameStart;

	// . set the node's hash -- used cuz it's faster than strcmp
	// . just hash the letters as upper case
	// . tag names are never utf8, so use the ascii ha
	m_hash = hash64Upper_a ( m_tagName , m_tagNameLen , 0LL);

	m_nodeId = setNodeInfo ( m_hash );

	// if we're pure xml, don't allow any html tags accept <!-- -->
	if ( pureXml ) {
		m_hasBackTag = true;
		m_isBreaking = true;
		m_isVisible  = true;
	}

	// . no back tag if / follow name
	// . this was only for "pureXml" but now i do it for all tags!
	if ( m_node[m_nodeLen - 2] == '/' || m_node[m_nodeLen - 2] == '?' ) {
		m_hasBackTag = false;
	}

	return m_nodeLen;
}

// . return the length of a node starting at "node"
int32_t getTagLen ( const char *node ) {
	// skip over first <
	int32_t i ;

	// . keep looping until we hit a < or > OR while we're in quotes
	// . ignore < and > when they're in quotes
	for ( i = 1 ; node[i] ; i++ ) {
		// this switch should speed things up... no!
		if ( node[i] != '<'  &&
		     node[i] != '>'  &&
		     node[i] != '\"' &&
		     node[i] != '\''  ) {
			continue;
		}

		if ( ( node[i] == '<' ) || ( node[i] == '>' ) ) {
			break;
		}

		// we can have double quotes within single quotes
		if ( node [ i ] == '\"' ) {
			// scan back looking for equal sign...
			int32_t k;

			for ( k = i - 1 ; k > 1 ; k-- ) {
				if ( is_wspace_a(node[k]) ) continue;
				break;
			}
			if ( k <= 1 ) continue;
			// . if an equal sign did not immediately preceed
			//   this double quote then ignore the double quote
			// . this now fixes the harwoodmuseum.org issue
			//   talked about below
			if ( node[k] != '=' ) continue;
			// skip over this first quote
			i++;
			while ( node[i] && node[i]!='\"' ) {
				// crap some pages have unbalanced quotes.
				// see /test/doc.14541556377486183454.html
				if ( node[i  ]=='>' && 
				     node[i-1]=='\"' ) {
					i--;
					break;
				}

				if ( node[i  ]=='>' && 
				     node[i-1]==' ' &&
				     node[i-2]=='\"' ) {
					i--;
					break;
				}

				// skip this char
				i++;
			}

			// return the length if tag ended abuptly
			if ( !node[i] ) {
				return i;
			}

			// back-to-back quotes? common mistake
			if ( node[i + 1] == '\"' ) {
				i++;
			}
			continue;
		}

		// continue if we don't have a " '" or "='"
		if ( node[i] != '\'' ) {
			continue;
		}

		if ( node[i - 1] != '=' && !is_wspace_a( node[i - 1] ) ) {
			continue;
		}

		// skip to end of quote
		while ( node[i] && node[i] != '\'' ) {
			i++;
		}
	}

	// skip i over the >
	if ( node[i] == '>' ) {
		i++;
	} else {
		// . else we found no closure outside of quotes so be more stringent
		// . look for closure with regard to quotes
		for ( i = 1; node[i] && node[i] != '>' && node[i] != '<'; i++ );
	}

	// return the LENGTH of the whole node
	return i;
}

int32_t XmlNode::setCommentNode ( char *node ) {
	m_nodeId      = TAG_COMMENT;
	m_isBreaking  = true;
	m_isVisible   = true;
	m_hasBackTag  = false;
	m_hash        = hash64 ( "!--" , 3 , 0LL );
	m_node        = node;
	m_tagName     = node + 1; // !--
	m_tagNameLen  = 3;

	// . compute node length
	// . TODO: do we have to deal with quotes????
	// . TODO: what about nested comments?
	int32_t i;
	for ( i = 3 ; node[i] ; i++ ) {
		if ( node[i]   !='>' ) continue;
		if ( node[i-1] !='-' ) continue;
		if ( node[i-2] =='-' ) break;
	}

	// skip i over the >, if any (could be end of doc)
	if ( node[i] == '>' ) i++;

	m_nodeLen = i;

	return i;
}

int32_t XmlNode::setCommentNode2 ( char *node ) {
	m_nodeId      = TAG_COMMENT;
	m_isBreaking  = false;//true;
	m_isVisible   = false;//true;
	m_hasBackTag  = false;
	m_hash        = hash64 ( "![" , 2 , 0LL );
	m_node        = node;
	m_tagName     = node + 1;
	m_tagNameLen  = 2;

	// . compute node length
	// . TODO: do we have to deal with quotes????
	// . TODO: what about nested comments?
	int32_t i;
	for ( i = 2 ; node[i] ; i++ ) {
		// look for ending of ]> like for <![if gt IE 6]>
		if ( node[i]   !='>' ) continue;
		if ( node[i-1] ==']' ) break;
		// look for ending of --> like for <![endif]-->
		if ( node[i-1] == '-' && node[i-2] == '-' ) break;
	}

	// skip i over the >, if any (could be end of doc)
	if ( node[i] == '>' ) i++;

	m_nodeLen = i;

	return i;
}

int32_t XmlNode::setCDATANode ( char *node ) {
	m_nodeId      = TAG_CDATA;
	m_isBreaking  = true;
	m_isVisible   = true;
	m_hasBackTag  = false;
	m_hash        = hash64 ( "![CDATA[" , 8 , 0LL );
	m_node        = node;
	m_tagName     = node + 1; // !--
	m_tagNameLen  = 8;

	// . compute node length
	// . TODO: do we have to deal with quotes????
	// . TODO: what about nested comments?
	int32_t i;
	for ( i = 8; node[i]; i++ ) {
		// seems like just ]] is good enough! don't need "]]>"
		if ( node[i] != ']' ) {
			continue;
		}

		if ( node[i + 1] != ']' ) {
			continue;
		}

		// but skip it if we got it
		if ( node[i + 2] != '>' ) {
			continue;
		}

		i += 3;
		break;
	}

	m_nodeLen = i;

	return i;
}

static bool findCharSingle( char nodeChar, char expectedChar, char expectedOtherChar, char *foundChar ) {
	if ( ( nodeChar == expectedChar ) || ( expectedOtherChar && nodeChar == expectedOtherChar ) ) {
		if ( foundChar ) {
			*foundChar = nodeChar;
		}

		return true;
	}

	// invalid char
	return false;
}

// allow whitespace when looking for char
static bool findChar( const char *node, int32_t start, int32_t end, int32_t *pos, char expectedChar,
					  char expectedOtherChar, char *foundChar, bool onlyAllowWhiteSpace ) {
	int32_t i;
	for ( i=start; i<end; i++ ) {
		if ( is_wspace_a( node[i] ) ) {
			continue;
		}

		if ( findCharSingle( node[i], expectedChar, expectedOtherChar, foundChar ) ) {
			*pos = i;
			return true;
		} else {
			if ( onlyAllowWhiteSpace ) {
				*pos = i;
				return false;
			}

			continue;
		}
	}

	*pos = i;
	return false;
}

static bool findChar( const char *node, int32_t start, int32_t end, int32_t *pos, char expectedChar,
					  bool onlyAllowWhiteSpace ) {
	return findChar(node, start, end, pos, expectedChar, '\0', NULL, onlyAllowWhiteSpace);
}

static bool findCharReverse( const char *node, int32_t start, int32_t end, int32_t *pos, char expectedChar,
                             char expectedOtherChar, char *foundChar, bool onlyAllowWhiteSpace ) {
	int32_t i;
	for ( i = start; i>end && i>0; i-- ) {
		if ( is_wspace_a( node[i] ) ) {
			continue;
		}

		if ( findCharSingle( node[i], expectedChar, expectedOtherChar, foundChar ) ) {
			*pos = i;
			return true;
		} else {
			if ( onlyAllowWhiteSpace ) {
				*pos = i;
				return false;
			}

			continue;
		}
	}

	*pos = i;
	return false;
}


static bool findCharReverse( const char *node, int32_t start, int32_t end, int32_t *pos, char expectedChar) {
	return findCharReverse(node, start, end, pos, expectedChar, '\0', NULL, true);
}

static bool findQuoteChar( const char *node, int32_t start, int32_t end, int32_t *pos, char *foundChar) {
	if (start > end) {
		return findCharReverse(node, start, end, pos, '\'', '"', foundChar, false);
	}

	return findChar(node, start, end, pos, '\'', '"', foundChar, false);
}

static bool findEqualChar( const char *node, int32_t start, int32_t end, int32_t *pos ) {
	if (start > end) {
		return findCharReverse(node, start, end, pos, '=');
	}

	return findChar(node, start, end, pos, '=', true);
}

static bool isValidAttrNameChar(char nodeChar) {
	if ( is_wspace_a( nodeChar ) || is_binary_a( nodeChar ) || nodeChar == '"' || nodeChar == '\'' ||
	     nodeChar == '<' || nodeChar == '>' || nodeChar == '/' || nodeChar == '=' ) {
		return false;
	}

	return true;
}

/**
 * Get attribute value
 *
 * The difference between XmlNode::getAttrValue and XmlNode::getFieldValue is that we try to recover
 * from bad attribute value
 *
 * @param[in] field Attribute name (only ascii supported)
 * @param[out] valueLen Attribute value length
 *
 * @return Attribute value
 */
char *XmlNode::getAttrValue( const char *field, int32_t fieldLen, int32_t *valueLen ) {
	if (valueLen) {
		*valueLen = 0;
	}

	/*
	 * https://www.w3.org/TR/html-markup/syntax.html#syntax-attributes
	 *
	 * attribute names:
	 *   must consist of one or more characters other than the space characters,
	 *   U+0000 NULL, """, "'", ">", "/", "=", the control characters, and any characters that are not defined by Unicode.
	 *
	 * attribute values:
	 *   can contain text and character references, with additional restrictions depending on whether they are
	 *   unquoted attribute values, single-quoted attribute values, or double-quoted attribute values.
	 *   Also, the HTML elements section of this reference describes further restrictions on the allowed values of
	 *   particular attributes, and attributes must have values that conform to those restrictions.
	 *
	 *   attributes can be specified in four different ways:
	 *     - empty attribute syntax (not handled)
	 *     - unquoted attribute-value syntax (not handled)
	 *     - single-quoted attribute-value syntax
	 *     - double-quoted attribute-value syntax
	 */

	bool found = false;

	int32_t startPos = 0;
	int32_t prevEndPos = 0;

	int32_t startQuotePos = 0;
	int32_t endQuotePos = 0;

	while ( startPos < m_nodeLen ) {
		char foundQuoteChar = '\0';

		// look for start quote (forward)
		found = findQuoteChar(m_node, startPos, m_nodeLen, &startQuotePos, &foundQuoteChar);
		if ( !found ) {
			// we should have at least one quote char to be able to have any value
			return NULL;
		}

		int32_t equalsPos = 0;

		// look for equals (reverse)
		found = findEqualChar( m_node, startQuotePos - 1, startPos, &equalsPos);
		if ( !found ) {
			// unable to find equals, assume dangling quote
			startPos = startQuotePos + 1;
			continue;
		}

		// look for end quote (forward)
		found = findChar(m_node, startQuotePos + 1, m_nodeLen, &endQuotePos, foundQuoteChar, false);
		if ( !found ) {
			// no end quote
			return NULL;
		}

		while (endQuotePos < m_nodeLen) {
			// do some validation

			// look for another quote
			int32_t nextQuotePos = 0;
			char nextFoundQuoteChar = '\0';

			found = findQuoteChar(m_node, endQuotePos + 1, m_nodeLen, &nextQuotePos, &nextFoundQuoteChar);
			if ( found ) {
				// let's see if it's preceeded by equals
				int32_t nextEqualsPos = 0;

				found = findEqualChar(m_node, nextQuotePos - 1, endQuotePos, &nextEqualsPos);
				if ( !found ) {
					// no preceding equals sign (assume invalid meta tag, try to recover from it)
					endQuotePos = nextQuotePos;
					continue;
				}
			}

			// assume found valid end quote
			break;
		}

		int32_t endAttrNamePos = equalsPos;

		// look for attr name (reverse)
		found = findCharReverse(m_node, equalsPos - 1, prevEndPos, &endAttrNamePos, field[fieldLen - 1]);
		if ( !found ) {
			// not our field
			prevEndPos = endQuotePos;
			startPos = endQuotePos + 1;
			continue;
		}

		int32_t startAttrNamePos = endAttrNamePos;

		// look for attr name start (forward)
		while ( isValidAttrNameChar( m_node[startAttrNamePos] ) && startAttrNamePos > prevEndPos ) {
			--startAttrNamePos;
		}

		if ( endAttrNamePos - startAttrNamePos != fieldLen ||
			 strncasecmp( &m_node[startAttrNamePos + 1], field, fieldLen ) != 0 ) {
			// no match
			found = false;
			prevEndPos = endQuotePos;
			startPos = endQuotePos + 1;
			continue;
		}

		// found match!
		found = true;
		break;
	}

	if (!found) {
		return NULL;
	}

	// set the length of the value
	if( valueLen ) {
		*valueLen = endQuotePos - startQuotePos - 1;
	}

	// return a ptr to the value
	return m_node + startQuotePos + 1;
}

// Return the value of the specified "field" within this node.
// the case of "field" does not matter.
char *XmlNode::getFieldValue ( const char *field , int32_t *valueLen ) {
	// reset this to 0
	*valueLen = 0;
	// scan for the field name in our node
	int32_t flen = strlen(field);
	char inQuotes = '\0';
	int32_t i;

	// scan the characters in the node, looking for the field name in ascii
	for ( i = 1; i + flen < m_nodeLen ; i++ ) {
		// skip the field if it's quoted
		if ( inQuotes) {
			if (m_node[i] == inQuotes ) {
				inQuotes = 0;
			}
			continue;
		}

		// set inQuotes to the quote if we're in quotes
		if ( (m_node[i]=='\"' || m_node[i]=='\'')) {
			inQuotes = m_node[i];
			continue;
		} 

		// a field name must be preceeded by non-alnum
		if ( is_alnum_a ( m_node[i-1] ) ) {
			continue;
		}

		// the first character of this field shout match field[0]
		if ( to_lower_a( m_node[i] ) != to_lower_a( field[0] ) ) {
			continue;
		}

		// field just be immediately followed by an = or space
		if ( m_node[i + flen] != '=' && !is_wspace_a( m_node[i + flen] ) ) {
			continue;
		}

		// field names must match
		if ( strncasecmp ( &m_node[i], field, flen ) != 0 ) {
			continue;
		}

		// break cuz we got a match for our field name
		break;
	}


	// return NULL if no matching field
	if ( i + flen >= m_nodeLen ) {
		return NULL;
	}

	// advance i over the fieldname so it pts to = or space
	i += flen;

	// advance i over spaces
	while ( i < m_nodeLen && is_wspace_a ( m_node[i] ) ) {
		i++;
	}

	// advance over the equal sign, return NULL if does not exist
	if ( i < m_nodeLen && m_node[i++] != '=' ) {
		return NULL;
	}

	// advance i over spaces after the equal sign
	while ( i < m_nodeLen && is_wspace_a ( m_node[i] ) ) {
		i++;
	}
	
	// now parse out the value of this field (could be in quotes)
	inQuotes = '\0';

	// set inQuotes to the quote if we're in quotes
	if ( m_node[i] == '\"' || m_node[i] == '\'' ) {
		inQuotes = m_node[i++];
	}

	// mark this as the start of the value
	int start = i;

	// advance i until we hit a space, or we hit a that quote if inQuotes
	if ( inQuotes ) {
		while ( i < m_nodeLen && m_node[i] != inQuotes ) {
			++i;
		}
	} else {
		while ( i < m_nodeLen && !is_wspace_a( m_node[i] ) && m_node[i] != '>' ) {
			++i;
		}
	}

	// set the length of the value
	*valueLen = i - start;

	// return a ptr to the value
	return m_node + start;
}

#include "HashTableX.h"

nodeid_t getTagId ( const char *s , NodeType **retp ) {
	// init table?
	static bool s_init = false;
	static HashTableX  s_ht;
	static char s_buf[10000];

	if ( ! s_init ) {
		s_init = true;
		s_ht.set ( 4 ,4,1024,s_buf,10000,false,"tagids");

		// how many NodeTypes do we have in g_nodes?
		static const int32_t nn = sizeof(g_nodes) / sizeof(NodeType);

		// set the hash table
		for ( int32_t i = 0 ; i < nn ; i++ ) {
			const char *name = g_nodes[i].m_nodeName;
			int32_t  nlen = strlen(name);
			int64_t h = hash64Upper_a ( name,nlen,0LL );
			NodeType *nt = &g_nodes[i];
			if ( ! s_ht.addKey(&h,&nt) ) {
				gbshutdownLogicError();
			}
		}

		// sanity
		if ( s_ht.m_numSlots != 1024 ) gbshutdownLogicError();

		// sanity test
		nodeid_t tt = getTagId ( "br" );
		if ( tt != TAG_BR ) {
			gbshutdownLogicError();
		}
	}

	// find end of tag name. hyphens are ok to be in name.
	// facebook uses underscores like <start_time>
	const char *e = s;
	for ( ; *e && (is_alnum_a(*e) || *e=='-'|| *e=='_'); e++);

	// hash it for lookup
	int64_t h = hash64Upper_a ( s , e - s , 0 );

	// look it up
	NodeType **ntp = (NodeType **)s_ht.getValue(&h);

	// assume none
	if ( retp ) {
		*retp = NULL;
	}

	// none?
	if ( ! ntp ) {
		return 0;
	}

	// got one
	if ( retp ) {
		*retp = *ntp;
	}

	// get id otherwise
	return (*ntp)->m_nodeId;
}

// . returns the nodeId
// . 0 means not a node
// . 1 means it's an xml node
// . > 1 is reserved for pre-defined html nodes
nodeid_t XmlNode::setNodeInfo ( int64_t  nodeHash ){
	// . we have a list of all node types called "g_nodes"
	// . each node type is a NodeType struct
	// . hash all these node types into a hash table by their node name
	// . we have 108 node names so we'll use 512 buckets
	// . given the hash of your node name you can look it up in this table
	static bool      s_isHashed = false;
	static int64_t s_hash [512];
	static nodeid_t  s_num  [512];

	// how many NodeTypes do we have in g_nodes?
	static const int32_t s_numNodeTypes = sizeof( g_nodes ) / sizeof( NodeType );

	// we only need to fill in the hash table once since it's static
	if ( !s_isHashed ) {
		// set this to true so we don't do the hashing again
		s_isHashed = true;

		// clear the hash table
		memset ( s_hash , 0 , 8*512 );
		// set the hash table
		for ( int32_t i = 0 ; i < s_numNodeTypes ; i++ ) {
			int64_t h = hash64Upper_a( g_nodes[i].m_nodeName, strlen( g_nodes[i].m_nodeName ), 0LL );
			int32_t b = (uint64_t)h & 511;

			while ( s_hash[b] ) {
				if ( ++b == 512 ) {
					b = 0;
				}
			}

			s_hash [ b ] = h;
			s_num  [ b ] = i;
		}
	}

	// look up nodeHash in hash table
	int32_t b = (uint64_t)nodeHash & 511;
	while ( s_hash[b] ) {
		if ( s_hash[b] == nodeHash ) {
			break;
		}

		if ( ++b == 512 ) {
			b = 0;
		}
	}

	// if it wasn't found it must be an xml node(or unrecognized html node)
	if ( ! s_hash[b] ) {
		// default is breaking, has back tag and is indexable
		m_isBreaking = true;
		m_hasBackTag = true;
		m_isVisible  = true;
		return 1; 
	}

	// otherwise extract the isBreaking and the nodeId from the hit bucket
	int32_t n = s_num[b];
	m_hasBackTag = g_nodes [ n ].m_hasBackTag;
	m_isBreaking = g_nodes [ n ].m_isBreaking;
	m_isVisible  = g_nodes [ n ].m_isVisible;

	// return the tag/node Id
	return g_nodes [ n ].m_nodeId;
}

int32_t getNumXmlNodes ( ) {
	return (int32_t)sizeof(g_nodes) / sizeof(XmlNode);
}

#include "Words.h" // BACKBITCOMP

bool isBreakingTagId ( nodeid_t tagId ) {
	return g_nodes [ tagId & BACKBITCOMP ].m_isBreaking;
}

bool hasBackTag ( nodeid_t tagId ) {
	return g_nodes [ tagId & BACKBITCOMP ].m_hasBackTag;
}
