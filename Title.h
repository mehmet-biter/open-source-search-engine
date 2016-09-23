// Matt Wells, copyright Aug 2005

#ifndef GB_TITLE_H
#define GB_TITLE_H

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

	bool setTitle( Xml *xml, Words *words, int32_t maxTitleLen, Query *query, LinkInfo *linkInfo, Url *firstUrl,
				   const char *filteredRootTitleBuf, int32_t filteredRootTitleBufSize, uint8_t contentType,
				   uint8_t langId );

	bool setTitleFromTags(Xml *xml, int32_t maxTitleLen , uint8_t contentType);

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

private:
	bool copyTitle(Words *words, int32_t t0, int32_t t1);
	float getSimilarity(Words *w1, int32_t i0, int32_t i1, Words  *w2, int32_t t0, int32_t t1);

	char m_title[MAX_TITLE_LEN];
	int32_t m_titleLen;

	int32_t  m_maxTitleLen;

	int32_t m_titleTagStart;
	int32_t m_titleTagEnd;
};

#endif // GB_TITLE_H
