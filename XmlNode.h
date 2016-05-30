#ifndef GB_XMLNODE_H
#define GB_XMLNODE_H

#include "gb-include.h"

// . an xml node can be text or tag (html or xml tag)

typedef int16_t nodeid_t;

// . get how many xml/html tags we have classified in our g_nodes[] array
int32_t getNumXmlNodes ( ) ;
bool isBreakingTagId ( nodeid_t tagId ) ;
bool hasBackTag ( nodeid_t tagId ) ;
int32_t getTagLen ( const char *node );

// s points to tag name - first char
nodeid_t getTagId ( const char *s , class NodeType **retp = NULL );

enum {
	TAG_TEXTNODE = 0,
	TAG_XMLTAG = 1,
	TAG_A = 2,
	TAG_ABBREV = 3,
	TAG_ACRONYM = 4,
	TAG_ADDRESS = 5,
	TAG_APPLET = 6,
	TAG_AREA = 7,
	TAG_AU = 8,
	TAG_AUTHOR = 9,
	TAG_B = 10,
	TAG_BANNER = 11,
	TAG_BASE = 12,
	TAG_BASEFONT = 13,
	TAG_BGSOUND = 14,
	TAG_BIG = 15,
	TAG_BLINK = 16,
	TAG_BLOCKQUOTE = 17,
	TAG_BQ = 18,
	TAG_BODY = 19,
	TAG_BR = 20,
	TAG_CAPTION = 21,
	TAG_CENTER = 22,
	TAG_CITE = 23,
	TAG_CODE = 24,
	TAG_COL = 25,
	TAG_COLGROUP = 26,
	TAG_CREDIT = 27,
	TAG_DEL = 28,
	TAG_DFN = 29,
	TAG_DIR = 30,
	TAG_DIV = 31,
	TAG_DL = 32,
	TAG_DT = 33,
	TAG_DD = 34,
	TAG_EM = 35,
	TAG_EMBED = 36,
	TAG_FIG = 37,
	TAG_FN = 38,
	TAG_FONT = 39,
	TAG_FORM = 40,
	TAG_FRAME = 41,
	TAG_FRAMESET = 42,
	TAG_H1 = 43,
	TAG_H2 = 44,
	TAG_H3 = 45,
	TAG_H4 = 46,
	TAG_H5 = 47,
	TAG_H6 = 48,
	TAG_HEAD = 49,
	TAG_HR = 50,
	TAG_HTML = 51,
	TAG_I = 52,
	TAG_IFRAME = 53,
	TAG_IMG = 54,
	TAG_INPUT = 55,
	TAG_INS = 56,
	TAG_ISINDEX = 57,
	TAG_KBD = 58,
	TAG_LANG = 59,
	TAG_LH = 60,
	TAG_LI = 61,
	TAG_LINK = 62,
	TAG_LISTING = 63,
	TAG_MAP = 64,
	TAG_MARQUEE = 65,
	TAG_MATH = 66,
	TAG_MENU = 67,
	TAG_META = 68,
	TAG_MULTICOL = 69,
	TAG_NOBR = 70,
	TAG_NOFRAMES = 71,
	TAG_NOTE = 72,
	TAG_OL = 73,
	TAG_OVERLAY = 74,
	TAG_P = 75,
	TAG_PARAM = 76,
	TAG_PERSON = 77,
	TAG_PLAINTEXT = 78,
	TAG_PRE = 79,
	TAG_Q = 80,
	TAG_RANGE = 81,
	TAG_SAMP = 82,
	TAG_SCRIPT = 83,
	TAG_SELECT = 84,
	TAG_SMALL = 85,
	TAG_SPACER = 86,
	TAG_SPOT = 87,
	TAG_STRIKE = 88,
	TAG_STRONG = 89,
	TAG_SUB = 90,
	TAG_SUP = 91,
	TAG_TAB = 92,
	TAG_TABLE = 93,
	TAG_TBODY = 94,

	TAG_TD = 95,
	TAG_TEXTAREA = 96,
	TAG_TEXTFLOW = 97,
	TAG_TFOOT = 98,
	TAG_TH = 99,
	TAG_THEAD = 100,
	TAG_TITLE = 101,
	TAG_TR = 102,
	TAG_TT = 103,

	TAG_U = 104,
	TAG_UL = 105,
	TAG_VAR = 106,
	TAG_WBR = 107,
	TAG_XMP = 108,
	TAG_COMMENT = 109,

	TAG_OPTION = 110,
	TAG_STYLE = 111,
	TAG_DOCTYPE = 112,
	TAG_XML = 113,
	TAG_START = 114,
	TAG_STOP = 115,
	TAG_SPAN = 116,
	TAG_LEGEND = 117,
	TAG_S = 118,

	TAG_ABBR = 119,
	TAG_CDATA = 120,
	TAG_NOSCRIPT = 121,
	TAG_FIELDSET = 122,
	TAG_FBORIGLINK = 123, // "feedburner:origlink" special feedburner link
	TAG_RDF = 124,      // rdf:RDF
	TAG_RSS = 125,      // rss
	TAG_FEED = 126,      // atom feed tag

	TAG_ITEM = 127,
	TAG_ENTRY = 128,
	TAG_CHANNEL = 129,
	TAG_ENCLOSURE = 130,
	TAG_WEBLOG = 131,
	// a tag we insert in XmlDoc.cpp to indicate expanded frame/iframe src
	TAG_GBFRAME = 132,
	TAG_TC = 133,
	TAG_GBXMLTITLE = 134,

	// facebook xml tags
	TAG_FBSTARTTIME = 135,
	TAG_FBENDTIME = 136,
	TAG_FBNAME = 137,
	TAG_FBPICSQUARE = 138,
	TAG_FBHIDEGUESTLIST = 139,

	// . do not parse this up into words!! it is text in <script> tags
	// . consider it a whole tag i guess
	TAG_SCRIPTTEXT = 140,
	TAG_BUTTON = 141,
	TAG_URLFROM = 142, // for ahrefs.com

	// support sitemap.xml
	TAG_LOC = 143,

	//
	// fake tags below here
	//
	// a fake tag used by Sections.cpp
	TAG_SENTENCE = 144,

	LAST_TAG
};

class XmlNode {
public:
	bool isText() {
		return m_nodeId == TAG_TEXTNODE;
	}

	bool isTag() {
		return m_nodeId > 0;
	}

	bool isHtmlTag() {
		return m_nodeId > 1;
	}

	bool isXmlTag() {
		return m_nodeId == TAG_XMLTAG;
	}

	nodeid_t getNodeId() {
		return m_nodeId;
	}

	int64_t getNodeHash() {
		return m_hash;
	}

	char *getNode() {
		return m_node;
	}

	// m_nodeLen is in bytes
	int32_t getNodeLen() {
		return m_nodeLen;
	}

	bool isBreaking() {
		return m_isBreaking;
	}

	bool isVisible() {
		return m_isVisible;
	}

	bool hasBackTag() {
		return m_hasBackTag;
	}

	// exclude meta tags and comment tags (they are not front or back)
	bool isFrontTag() {
		return m_nodeId > 0 && m_node[1] != '/' && m_nodeId != TAG_META && m_nodeId != TAG_COMMENT;
	}

	char* getAttrValue(const char *field, int32_t fieldLen, int32_t *valueLen );

	// . get the value of a field like "href" in the <a href="blah"> tag
	char *getFieldValue ( const char *fieldName , int32_t *valueLen );

	// . used exclusively by Xml class which contains an array of XmlNodes
	// . "node" points to the beginning of the node, the '<' if it's a tag
	// . sets m_node,m_nodeLen,m_hash,m_isBreaking,m_nodeId
	// . returns the length of the node
	// . pureXml is true if node cannot be an html tag, except comment
	//int32_t set ( char *node , bool pureXml );
	int32_t set (char *node , bool pureXml );

	// . called by set() to get the length of a COMMENT node (and set it)
	int32_t setCommentNode ( char *node );

	int32_t setCommentNode2 ( char *node );

	// . called by set() to get the length of a CDATA node (and set it)
	int32_t setCDATANode ( char *node );

	// . called by set() to get nodeId and isBreaking of a tag node
	// . returns the nodeId
	nodeid_t setNodeInfo    ( int64_t  nodeHash );

	char *m_node;	  // tag data, or text data if not a tag
	int32_t m_nodeLen; // m_nodeLen is in bytes
	char *m_tagName;   // iff this node is a tag
	int32_t m_tagNameLen;
	int64_t m_hash;	// iff this node is a tag
	int16_t m_depth;   // set by Xml class (xml depth only)
	nodeid_t m_nodeId; // 0 for text,1 for xml tag, 1+ for html
	unsigned char m_hasBackTag : 1;
	unsigned char m_isBreaking : 1; // does tag (if it is) line break?
	unsigned char m_isVisible : 1;
	unsigned char m_isSelfLink : 1; // an a href tag link to self?
	int32_t m_pairTagNum;			// paired opening or closing tag
	class XmlNode *m_parent;
};

// . does "s" start a tag? (regular tag , back tag or comment tag)
inline bool isTagStart ( const char *s ) {
	// it must start with < to be a tag
	if ( s[0] != '<' ) {
		return false;
	}

	// next char can be an alnum, !-- or / then alnum

	// Extensible Markup Language (XML) 1.0 (Fifth Edition)
	// https://www.w3.org/TR/REC-xml/#NT-Name
	// NameStartChar ::= ":" | [A-Z] | "_" | [a-z] | [#xC0-#xD6] | [#xD8-#xF6] | [#xF8-#x2FF] |
	//                   [#x370-#x37D] | [#x37F-#x1FFF] | [#x200C-#x200D] | [#x2070-#x218F] |
	//                   [#x2C00-#x2FEF] | [#x3001-#xD7FF] | [#xF900-#xFDCF] | [#xFDF0-#xFFFD] |
	//                   [#x10000-#xEFFFF]

	/// @todo ALC cater for other start characters
	/// regex: "^<[A-Za-z]"
	if ( is_alpha_a( s[1] ) ) {
		return true;
	}

	// next char can be 1 of 3 things to be a tag
	// / is also acceptable, followed only by an alnum or >

	/// @todo ALC "</>" a valid tag?
	/// regex: "^</[A-Za-z0-9>]"
	if ( s[1] == '/' ) {
		if ( is_alnum_a( s[2] ) || (s[2] == '>') ) {
			return true;
		}
		return false;
	}

	// office.microsoft.com uses <?xml ...?> tags
	/// regex: "^<?[A-Za-z0-9]"
	if ( s[1]=='?' ) {
		if ( is_alnum_a(s[2]) ) {
			return true;
		}
		return false;
	}

	// make sure the double hyphens follow the ! or alnum
	if ( s[1]=='!' ) {
		// this is for <!xml> i guess
		if ( is_alnum_a(s[2]) ) {
			return true;
		}

		// and the <![CDATA[
		// and <![....]> i've seen too
		// <![if gt IE 6]><script>.... for waterfordcoc.org
		if ( s[2] == '[' ) {
			return true;
		}

		// and the <!-- comment here--> famous comment tag
		if ( s[2]=='-' && s[3]=='-' ) {
			return true;
		}
	}

	return false;
}

enum NodeTagType {
	TAG_TYPE_UNKNOWN = 0,
	TAG_TYPE_XML,
	TAG_TYPE_HTML_VOID,
	TAG_TYPE_HTML_RAW,
	TAG_TYPE_HTML_ESCAPABLE_RAW,
	TAG_TYPE_HTML_FOREIGN,
	TAG_TYPE_HTML_NORMAL,
	LAST_TAG_TYPE
};

// Now set up a structure for describing ALL the available HTML nodes.
// . Each HTML node has a name, name length, does it break a word?
//   a format bit. (most HTML tags have 0 for their format bit
//   because we really don't care about what they do -- we use format
//   bits for extracting title, summaries, et al.
// . the is indexable is false for tags like <script> <option> whose contents
//   are not visible/indexable
class NodeType {
public:
	const char *m_nodeName;
	bool m_hasBackTag;
	char m_isBreaking;
	char m_isVisible;
	char m_filterKeep1; // for &strip=1 option
	char m_filterKeep2; // for &strip=2 option
	nodeid_t m_nodeId;
	char m_tagType;
};

extern class NodeType g_nodes[];

static inline const char *getTagName( nodeid_t tagId ) {
	return g_nodes[tagId].m_nodeName;
}

#endif // GB_XMLNODE_H

