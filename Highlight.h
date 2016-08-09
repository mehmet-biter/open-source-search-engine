// Matt Wells, copyright Jul 2001

// . highlights the terms in Query "q" in "xml" and puts results in m_buf

#ifndef GB_HIGHLIGHT_H
#define GB_HIGHLIGHT_H

#include "Words.h"
#include "Query.h"
#include "Matches.h"
#include "Xml.h"
#include "Url.h"

class Highlight {
public:

	// . content is an html/xml doc
	// . we highlight Query "q" in "xml" as best as we can
	// . store highlighted text into "buf"
	// . return length stored into "buf"
	int32_t set( SafeBuf *sb, char *content, int32_t contentLen, Query *q, const char *frontTag,
				  const char *backTag, int32_t niceness );

	int32_t set( SafeBuf *sb, const Words *words, const Matches *matches, const char *frontTag = NULL,
				 const char *backTag = NULL, const Query *q = NULL );

	int32_t getNumMatches() { return m_numMatches; }

 private:
	bool highlightWords ( const Words *words, const Matches *m, const Query *q=NULL );

	class SafeBuf *m_sb;

	const char    *m_frontTag;
	const char    *m_backTag;
	int32_t     m_frontTagLen;
	int32_t     m_backTagLen;

	int32_t m_numMatches;
};

#endif // GB_HIGHLIGHT_H

