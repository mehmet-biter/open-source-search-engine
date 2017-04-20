// Gigablast, Inc., Copyright Mar 2007

#ifndef GB_PROCESS_H
#define GB_PROCESS_H

#include <inttypes.h>
#include <stddef.h>


class Process {

 public:

	bool getFilesToCopy ( const char *srcDir , class SafeBuf *buf ) ;
	bool checkFiles ( const char *dir );

	// . the big save command
	// . does not save everything, just the important stuff
	bool save ( );

	// . this will save everything and exit
	// . urgent is true if we cored
	bool shutdown ( bool urgent, void  *state = NULL, void (*callback) (void *state ) = NULL);

	static const char *getAbortFileName() {
		return "./fatal_error";
	}

	/**
	 * Abort process
	 *
	 * @param save_on_abort Save data to disk on abort
	 */
	[[ noreturn ]] void shutdownAbort ( bool save_on_abort = false );

	bool checkNTPD();

	Process                 ( ) ;
	bool init               ( ) ;
	bool isAnyTreeSaving    ( ) ;
	bool save2              ( ) ;
	bool shutdown2          ( ) ;
	void disableTreeWrites  ( bool shuttingDown ) ;
	void enableTreeWrites();

	bool isShuttingDown() const { return m_mode == EXIT_MODE; }
	bool isRdbDumping       ( ) ;
	bool isRdbMerging       ( ) ;
	bool saveRdbTrees(bool shuttingDown);
	bool saveRdbIndexes();
	bool saveRdbMaps();
	bool saveBlockingFiles1 ( ) ;
	bool saveBlockingFiles2 ( ) ;
	void resetAll           ( ) ;
	void resetPageCaches    ( ) ;
	double getLoadAvg	( );

	int64_t getTotalDocsIndexed();
	int64_t m_totalDocsIndexed;

	class Rdb *m_rdbs[32];
	int32_t       m_numRdbs;
	bool       m_urgent;
	enum {
		NO_MODE   = 0,
		EXIT_MODE = 1,
		SAVE_MODE = 2,
		LOCK_MODE = 3
	} m_mode;
	int64_t  m_lastSaveTime;
	int64_t  m_processStartTime;
	bool       m_sentShutdownNote;
	bool       m_blockersNeedSave;
	bool       m_repairNeedsSave;
	int32_t       m_try;
	int64_t  m_firstShutdownTime;

	void        *m_callbackState;
	void       (*m_callback) (void *state);

	// a timestamp for the sig alarm handler in Loop.cpp
	int64_t m_lastHeartbeatApprox;

	void callHeartbeat ();

	bool m_suspendAutoSave;

	bool        m_exiting;
	bool        m_calledSave;

	float m_diskUsage;
	int64_t m_diskAvail;
};

extern bool g_inAutoSave;
extern Process g_process;

#endif // GB_PROCESS_H
