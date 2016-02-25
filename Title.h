// Matt Wells, copyright Aug 2005

#ifndef _TITLE_H_
#define _TITLE_H_

#include <stdint.h>

#define MAX_TITLE_LEN 2048

// forward declaration
class XmlDoc;
class Xml;
class Words;
class Query;
class LinkInfo;
class Url;

class Title {
public:
	Title();
	~Title();

	void reset();

	/// @todo ALC correct comments

	// . set m_title to the title of the document represented by "xd"
	// . if getHardTitle is true will always use the title in the <title>
	//   tag, but if that is not present, will resort to trying to guess 
	//   a title from the document content or incoming link text.
	// . uses the following:
	// .   title tag
	// .   meta title tag
	// .   incoming link text
	// .   <hX> tags at the top of the scored content
	// .   blurbs in bold at the top of the scored content
	// . removes starting/trailing punctuation when necessary
	// . does not consult words with scores of 0 (unless a meta tag)
	// . maxTitleChars is in chars (a utf16 char is 2 bytes or more)
	// . maxTitleChars of -1 means no max
	bool setTitle( Xml *xml, Words *words, int32_t maxTitleChars, Query *q, LinkInfo *linkInfo, Url *firstUrl,
				   char **filteredRootTitleBuf, int32_t filteredRootTitleBufSize, uint8_t contentType,
				   uint8_t langId, int32_t niceness );

	char *getTitle() {
		return m_title;
	}

	// does NOT include \0
	int32_t getTitleLen() {
		return m_titleLen;
	}

	int32_t getTitleTagStart() {
		return m_titleTagStart;
	}

	int32_t getTitleTagEnd() {
		return m_titleTagEnd;
	}

	bool copyTitle(Words *words, int32_t t0, int32_t t1);

	float getSimilarity(Words *w1, int32_t i0, int32_t i1, Words  *w2, int32_t t0, int32_t t1);

private:
	bool setFromTags(Xml *xml, int32_t maxTitleLen);

	bool setTitle4( Xml *xml, Words *words, int32_t maxTitleChars, int32_t maxTitleWords,
					Query *q, LinkInfo *info, Url *firstUrl, char **filteredRootTitleBuf,
					int32_t filteredRootTitleBufSize, uint8_t contentType, uint8_t langId );

	char m_title[MAX_TITLE_LEN];
	int32_t m_titleLen;

	char m_niceness;

	int32_t  m_maxTitleChars;

	int32_t m_titleTagStart;
	int32_t m_titleTagEnd;
};

#endif
