#ifndef GB_PROFILER_H
#define GB_PROFILER_H

#include "Parms.h"
#include "SafeBuf.h"
#include "HashTableX.h"

struct FnInfo {
	char m_fnName[256];
	int64_t     m_startTime;
};

struct HitEntry {
	char *file;
	uint32_t line;
	uint32_t address;
};

class FrameTrace {
	public:
		FrameTrace();
		~FrameTrace();
		FrameTrace *set(const uint32_t addr);
		FrameTrace *add(const uint32_t baseAddr);
		void dump(	SafeBuf *out,
				const uint32_t level = 0,
				uint32_t printStart = 0) const;
		uint32_t getPrintLen(const uint32_t level = 0) const;
		uint32_t address;
		static const uint16_t MAX_CHILDREN = 64;
		FrameTrace *m_children[MAX_CHILDREN];
		uint32_t m_numChildren;
		uint32_t hits;
};

class Profiler {
 public:
	Profiler();
	~Profiler();

	bool reset();
	bool init();

	bool printRealTimeInfo(SafeBuf *sb, const char *coll);

	char *getFnName(PTRTYPE address,int32_t *nameLen=NULL);

	void getStackFrame();

	void startRealTimeProfiler();

	void stopRealTimeProfiler(bool keepData);

	void cleanup();
	
	FrameTrace *getNewFrameTrace(const uint32_t addr);

	bool m_realTimeProfilerRunning;

	SafeBuf m_ipBuf;

protected:
	HashTableX m_fn;

	int32_t m_totalFrames;
 public://private:
	// Realtime profiler stuff
	uint32_t rtNumEntries;
	HitEntry *hitEntries;
	uint32_t *m_addressMap;
	uint32_t m_addressMapSize;
	FrameTrace *m_rootFrame;
	uint32_t m_lastDeltaAddress;
	uint64_t m_lastDeltaAddressTime;
	static const uint32_t MAX_FRAME_TRACES = 1024 * 64;
	FrameTrace *m_frameTraces;
	uint32_t m_numUsedFrameTraces;
};

extern Profiler g_profiler;


#endif // GB_PROFILER_H
