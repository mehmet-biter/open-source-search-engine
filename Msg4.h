// Gigablast, Inc., copyright Jul 2007

// like Msg1.h but buffers up the add requests to avoid packet storms

#ifndef GB_MSG4_H
#define GB_MSG4_H

bool saveAddsInProgress ( const char *filenamePrefix );
bool loadAddsInProgress ( const char *filenamePrefix );
// used by Repair.cpp to make sure we are not adding any more data ("writing")
bool hasAddsInQueue     ( ) ;

#include "SafeBuf.h"
#include "rdbid_t.h"

class Msg4 {
public:
	Msg4()
		: m_inUse(false) {
	}

	// why wasn't this saved in addsinprogress.dat file?
	~Msg4() {
		if (m_inUse) {
			log(LOG_ERROR, "BAD: MSG4 in use!!!!!! this=%p", this);
		}
	}

	bool addMetaList(SafeBuf *sb, collnum_t collnum, void *state,
	                 void (*callback)(void *state), rdbid_t rdbId = RDB_NONE, int32_t shardOverride = -1);

	// this one is faster...
	// returns false if blocked
	bool addMetaList(const char *metaList, int32_t metaListSize, collnum_t collnum, void *state,
	                 void (*callback)(void *state), rdbid_t rdbId = RDB_NONE, int32_t shardOverride = -1);

	bool isInUse() const { return m_inUse; }

	static bool registerHandler();
	static bool isInLinkedList(Msg4 *msg4);
	static void storeLineWaiters();

	SafeBuf m_tmpBuf;

private:
	bool addMetaList2();

	void (*m_callback )(void *state);
	void *m_state;

	rdbid_t m_rdbId;
	bool m_inUse;
	collnum_t m_collnum;

	int32_t m_shardOverride;

	const char *m_metaList;
	int32_t m_metaListSize;
	const char *m_currentPtr; // into m_metaList

	// the linked list for waiting in line
	Msg4 *m_next;
};

#endif // GB_MSG4_H
