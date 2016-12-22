// Matt Wells, copyright Jul 2001

// . gets xhtml filtered into plain text
// . parses plain text into Words
// . gets rawTermIds from the query
// . uses Matches class to find words that match rawTermIds
// . for each term in query, find the line with that term and the most
//   other matching terms, and print that line

// . modifications...
// . exclude title from the plain text (call xml->getText() twice?)
// . find up to X lines
// . find phrases by setting the Phrases class as well
// . score lines by termfreqs of terms you highlight in the line
// . highlight terms in order of their termFreq, lowest first!
// . remove junk from start/end of summary (no back-to-back punct)
// . stop summary line on space, not non-alnum (no breaking on apostrophe)
// . don't highlight stop words????

#ifndef GB_SUMMARY_H
#define GB_SUMMARY_H

#include "gb-include.h"

#define MAX_SUMMARY_LEN (1024*20)
#define MAX_SUMMARY_EXCERPTS 1024

class XmlDoc;
class Sections;
class Matches;
class Xml;
class Words;
class Pos;
class Query;
class Url;

class Summary {
public:
	Summary();
	~Summary();

	bool setSummary(const Xml *xml, const Words *words, const Sections *sections, Pos *pos, const Query *q,
	                int32_t maxSummaryLen, int32_t numDisplayLines, int32_t maxNumLines, int32_t maxNumCharsPerLine,
	                const Url *f, const Matches *matches, const char *titleBuf, int32_t titleBufLen);

	bool setSummaryFromTags(Xml *xml, int32_t maxSummaryLen, const char *titleBuf, int32_t titleBufLen);

	char       *getSummary();
	const char *getSummary() const;
	int32_t getSummaryDisplayLen() const;
	int32_t getSummaryLen() const;

	bool isSetFromTags() const;

private:
	bool verifySummary(const char *titleBuf, int32_t titleBufLen);

	bool getDefaultSummary(const Xml *xml, const Words *words, const Sections *sections, Pos *pos, int32_t maxSummaryLen);

	int64_t getBestWindow (const Matches *matches, int32_t mn, int32_t *lasta, int32_t *besta, int32_t *bestb,
	                       char *gotIt, char *retired, int32_t maxExcerptLen );

	// null terminate and store the summary here.
	char  m_summary[ MAX_SUMMARY_LEN ];
	int32_t  m_summaryLen;
	int32_t  m_summaryExcerptLen[ MAX_SUMMARY_EXCERPTS ];
	int32_t  m_numExcerpts;

	// if getting more lines for deduping than we need for displaying,
	// how big is that part of the summary to display?
	int32_t m_numDisplayLines;
	int32_t m_displayLen;

	int32_t  m_maxNumCharsPerLine;

	bool m_isSetFromTags;

	// ptr to the query
	const Query     *m_q;

	float *m_wordWeights;
	int32_t m_wordWeightSize;
	char m_tmpWordWeightsBuf[128];

	char *m_buf4;
	int32_t m_buf4Size;
	char m_tmpBuf4[128];
};

#endif

