// Gigablast, Inc., copyright Jul 2007

// like Msg1.h but buffers up the add requests to avoid packet storms

#ifndef GB_MSG4OUT_H
#define GB_MSG4OUT_H

bool saveAddsInProgress ( const char *filenamePrefix );
bool loadAddsInProgress ( const char *filenamePrefix );
// used by Repair.cpp to make sure we are not adding any more data ("writing")
bool hasAddsInQueue     ( ) ;

#include "rdbid_t.h"
#include "types.h"
#include <vector>

class SafeBuf;

class Msg4 {
public:
	Msg4();
	~Msg4();

	bool addMetaList(SafeBuf *sb, collnum_t collnum, void *state,
	                 void (*callback)(void *state), rdbid_t rdbId = RDB_NONE, int32_t shardOverride = -1);

	// returns false if blocked
	bool addMetaList(const char *metaList, int32_t metaListSize, collnum_t collnum, void *state,
	                 void (*callback)(void *state), rdbid_t rdbId = RDB_NONE, int32_t shardOverride = -1);

	bool isInUse() const { return m_inUse; }

	static bool initializeOutHandling();
	static bool isInLinkedList(const Msg4 *msg4);
	static void storeLineWaiters();

private:
	void prepareMetaList();
	bool processMetaList();

	struct RdbItem {
		RdbItem(rdbid_t rdbId, int32_t hostId, const char *key, int32_t recSize)
			: m_rdbId(rdbId)
			, m_hostId(hostId)
			, m_key(key)
			, m_recSize(recSize) {
		}

		rdbid_t m_rdbId;
		int32_t m_hostId;
		const char *m_key;
		int32_t m_recSize;
	};


	void (*m_callback )(void *state);
	void *m_state;

	rdbid_t m_rdbId;
	bool m_inUse;
	collnum_t m_collnum;

	int32_t m_shardOverride;

	const char *m_metaList;
	int32_t m_metaListSize;

	size_t m_destHostId;
	std::vector<std::pair<int32_t, std::vector<RdbItem>>> m_destHosts;
};

#endif // GB_MSG4OUT_H
