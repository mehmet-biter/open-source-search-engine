// Matt Wells, copyright July 2001

#ifndef GB_MSG2_H
#define GB_MSG2_H

#include "Query.h"
#include "Msg0.h"
#include "Msg5.h"
#include "max_niceness.h"

/** define the max # of lists you can get as the max # of query terms for now */
//#define MAX_NUM_LISTS MAX_QUERY_TERMS
/** how many outstanding msg5 requests at one time? */
//#define MSG2_MAX_REQUESTS MAX_QUERY_TERMS
#define MSG2_MAX_REQUESTS 32
/** support the &sites=xyz.com+abc.com+... to restrict search results to provided sites.*/
#define MAX_WHITELISTS 500


class Msg39Request;

/**
 *
 * Msg2 is the "message" that gets the term-list from the disk.
 * This is one fundamental brick of the search, given a
 * query term, it returns the various documents that contains it from
 * the disk. The list returned takes the form of a RdbList of PosDB keys.
 *
 * This is used by Msg39 (which perform retrieval+intersection+ranking, see its doc).
 * The main method of this message is getLists().
 *
 * For efficiency, this method actually retrieve several RdbList at once, corresponding
 * to the different query terms. Msg2 possess a copy of the RdbList returned:
 *
 *
 */
class Msg2 {

public:

	Msg2();
	void reset();

	/** main function of Msg2. Fetch the term list (one per query term), and store them
	 *  in "lists". A copy is kept in this->m_lists
	 *  returns false if blocked, true otherwise.
	 *  sets "errno" on error.
	 *  "termIds/termFreqs" should NOT be on the stack in case we block
	 */
	bool getLists(int32_t rdbId,
			collnum_t collnum,			//char    *coll        ,
			bool addToCache,
			const QueryTerm *qterms,
			int32_t numQterms,
			// restrict search results to this list of sites,
			// i.e. "abc.com+xyz.com+..." (Custom Search)
			char *whiteList,
			// for intersecting ranges of docids separately
			// to prevent OOM errors
			int64_t docIdStart,
			int64_t docIdEnd,
			int32_t *minRecSizes,
			RdbList *lists,
			void *state,
			void (*callback)(void *state),
			Msg39Request *request,
			int32_t niceness = MAX_NICENESS,
			bool isDebug = false);

	/** Get the list "i". Once we got the lists, (getLists(...) has been called), we cache them in m_lists.*/
	RdbList *getList(int32_t i) {
		return &m_lists[i];
	}

	/** return the number of lists == the number of query terms */
	int32_t getNumLists() const {
		return m_numLists;
	}

	int64_t docIdStart() const { return m_docIdStart; }
	int64_t docIdEnd() const { return m_docIdEnd; }

	int32_t getNumWhiteLists() const { return m_w; }
	RdbList *getWhiteList(int32_t i) { return &(m_whiteLists[i]); }

private:
	// helper (handles index of list)
	int32_t m_i;

	// list of sites to restrict search results to. space separated
	char *m_whiteList;
	int64_t m_docIdStart;
	int64_t m_docIdEnd;
	char *m_p;
	int32_t m_w;
	RdbList m_whiteLists[ MAX_WHITELISTS];

	// internal helper method that actually does the fetching of the lists
	bool getLists();

	Msg5 *getAvailMsg5();
	void returnMsg5(Msg5 *msg5);

	bool gotList(RdbList *list);

	// we can get up to MAX_QUERY_TERMS term frequencies at the same time
	Msg5 m_msg5[ MSG2_MAX_REQUESTS];
	bool m_avail[ MSG2_MAX_REQUESTS]; // which msg0s are available?

	int32_t m_errno;

	RdbList *m_lists;

	const QueryTerm *m_qterms;
	int32_t *m_minRecSizes;
	int32_t m_numLists;
	bool m_getComponents;
	char m_rdbId;
	bool m_addToCache;collnum_t m_collnum;
	bool m_checkCache;
	int32_t m_k;

	// the parent Msg39Request
	Msg39Request *m_req;

	int32_t m_numReplies;
	int32_t m_numRequests;

public: //public for callback
	void gotListWrapper( Msg5 *msg5 );
private:
	void *m_state;
	void (*m_callback)(void *state);
	int32_t m_niceness;

	// if this is true we log more output
	bool m_isDebug;

	// start time
	int64_t m_startTime;
};

#endif // GB_MSG2_H
