// Matt Wells, Copyright June 2002


// . this calls let's us use threads

#ifndef GB_THREADS_H
#define GB_THREADS_H


#include <sys/types.h>  // pid_t

// if we are a thread this gets the threadid, otherwise, the main process id
//pid_t getpidtid();
// on 64-bit architectures pthread_t is 64 bits and pid_t is still 32 bits
pthread_t getpidtid();

// user-defined thread types
enum
{
	THREAD_TYPE_DISK,
	THREAD_TYPE_MERGE,
	THREAD_TYPE_INTERSECT,
	THREAD_TYPE_FILTER,
	THREAD_TYPE_SAVETREE,
	THREAD_TYPE_UNLINK,
	THREAD_TYPE_GENERIC,
	THREAD_TYPE_MAX
};


#define MAX_NICENESS     2

// . a ThreadQueue has a list of thread entries
// . each thread entry represents a thread in progress or waiting to be created
class ThreadEntry {
public:
	int32_t         m_niceness                 ;
	void        (* m_callback)(void *state,class ThreadEntry *) ;
	void         *m_state                   ;
	// returns a void * :
	void        *(* m_startRoutine)(void *,class ThreadEntry *) ;
	bool         m_isOccupied               ; // is thread waiting/going?
	bool         m_isLaunched               ; // has it been launched?
	bool         m_isDone                   ; // is it done running?
	bool         m_readyForBail             ; // BigFile.cpp stuck reads
	char        *m_allocBuf                 ; // BigFile.cpp stuck reads
	int32_t      m_allocSize                ; // BigFile.cpp stuck reads
	int32_t         m_errno                    ; // BigFile.cpp stuck reads
	int32_t         m_bytesToGo                ; // BigFile.cpp stuck reads
	int64_t    m_queuedTime               ; // when call() was called
	int64_t    m_launchedTime             ; // when thread was launched
	int64_t    m_preExitTime              ; // when thread was about done
	int64_t    m_exitTime                 ; // when thread was done
	char         m_qnum                     ; // what thread queue we r in
	char         m_doWrite                  ; // BigFile.cpp stuck reads

	char        *m_stack                    ;
	int32_t         m_stackSize                ;
	int32_t         m_si                       ; // s_stackPtrs[i] = m_stack

	bool      m_needsJoin;
	pthread_t m_joinTid;

	class ThreadEntry *m_nextLink;
	class ThreadEntry *m_prevLink;

	// the waiting linked list we came from
	ThreadEntry **m_bestHeadPtr;
	ThreadEntry **m_bestTailPtr;
};



// our Thread class has one ThreadQueue per thread type
class ThreadQueue {
public:
	// what type of threads are in this queue (used-defined)?
	char         m_threadType;
	// how many threads have been launched total over time?
	int64_t    m_launched;
	// how many threads have returned total over time?
	int64_t    m_returned;
	// how many can we launch at one time?
	int32_t         m_maxLaunched;

	// the list of entries in this queue
	//ThreadEntry  m_entries [ MAX_THREAD_ENTRIES ];
	ThreadEntry *m_entries ;
	int32_t         m_entriesSize;
	int32_t         m_maxEntries;

	// linked list head for launched thread entries
	ThreadEntry *m_launchedHead;

	// linked list head for empty thread entries
	ThreadEntry *m_emptyHead;

	// heads/tails for linked lists of thread entries waiting to launch
	ThreadEntry *m_waitHead0;
	ThreadEntry *m_waitHead1;
	ThreadEntry *m_waitHead2;
//	ThreadEntry *m_waitHead3;
	ThreadEntry *m_waitHead4;
	ThreadEntry *m_waitHead5;
	ThreadEntry *m_waitHead6;

	ThreadEntry *m_waitTail0;
	ThreadEntry *m_waitTail1;
	ThreadEntry *m_waitTail2;
//	ThreadEntry *m_waitTail3;
	ThreadEntry *m_waitTail4;
	ThreadEntry *m_waitTail5;
	ThreadEntry *m_waitTail6;


	bool init (char threadType, int32_t maxThreads, int32_t maxEntries);

	ThreadQueue();
	void reset();

	int32_t getNumWriteThreadsOut() ;


	// . for adding an entry
	// . returns false and sets errno on error
	ThreadEntry *addEntry ( int32_t   niceness,
				void  *state                        ,
				void  (* callback    )(void *state,
						       class ThreadEntry *t) ,
				void *(* startRoutine)(void *state,
						       class ThreadEntry *t) );
	// calls the callback of threads that are done (exited) and then
	// removes them from the queue
	bool         cleanUp      ( ThreadEntry *tt , int32_t maxNiceness );
	bool         timedCleanUp ( int32_t maxNiceness );

	void bailOnReads ();
	bool isHittingFile ( class BigFile *bf );

	// . launch a thread from our queue
	// . returns false and sets errno on error
	bool         launchThread2 ( );

	bool launchThreadForReals ( ThreadEntry **headPtr ,
				    ThreadEntry **tailPtr ) ;

	void removeThreads2 ( ThreadEntry **headPtr ,
			      ThreadEntry **tailPtr ,
			      class BigFile *bf ) ;

	void print ( ) ;

	// this is true if low priority threads are temporarily suspended
	bool m_isLowPrioritySuspended ;

	// return m_threadType as a NULL-terminated string
	const char *getThreadType () const;

	void removeThreads ( class BigFile *bf ) ;
};



// this Threads class has a list of ThreadQueues, 1 per thread type
class Threads {
public:
	Threads();

	// returns false and sets errno on error, true otherwise
	bool init();

	int32_t getStack ( ) ;
	void returnStack ( int32_t si );
	void setPid();
	void reset ( ) ;

	// . we restrict the # of threads based on their type
	// . for instance we allow up to say, 5, disk i/o threads,
	//   but only 1 thread for doing IndexList intersections
	// . threads with higher niceness values always wait for any thread
	//   with lower niceness to complete
	// . returns false and sets errno on error, true otherwise
	bool registerType ( char type , int32_t maxThreads , int32_t maxEntries );

	// is the caller a thread?
	bool amThread ( );

	void printQueue ( int32_t q ) { m_threadQueues[q].print(); };
	void printState();

	// disable all threads... no more will be created, those in queues
	// will never be spawned
	void disableThreads () { m_disabled = true;  };
	void enableThreads  () { m_disabled = false; };
	bool areThreadsDisabled() { return m_disabled; };
	bool areThreadsEnabled () { return ! m_disabled; };

	// . returns false and sets errno if thread launch failed
	// . returns true on success
	// . when thread is done a signal will be put on the g_loop's
	//   sigqueue to call "callback" with "state" as the parameter
	// . niceness deteremines the niceness of this signal as well as
	//   the thread's priority
	bool call ( char		type,
				int32_t   	niceness,
				void  		*state,
				void  		(* threadDoneCallback)(void *state, class ThreadEntry *t) ,
				void 		*(* startRoutine      )(void *state, class ThreadEntry *t) );

	// try to launch threads waiting to be launched in any queue
	int32_t launchThreads ();

	// call cleanUp() for each thread queue
	bool cleanUp ( ThreadEntry *tt , int32_t maxNiceness ) ;

	void bailOnReads ();
	bool isHittingFile ( class BigFile *bf ) ;

	//calls callbacks and launches all threads
	int32_t timedCleanUp (int32_t maxTime, int32_t niceness );//= MAX_NICENESS);

	// . gets the number of disk threads (seeks) and total bytes to read
	// . ignores disk threads that are too nice (over maxNiceness)
	int32_t getDiskThreadLoad ( int32_t maxNiceness , int32_t *totalToRead ) ;

	const ThreadQueue* getThreadQueue(int type) const { return &m_threadQueues[type]; }
	int32_t      getNumThreadQueues() { return m_numQueues; }

	// all high priority threads...
	int32_t getNumActiveHighPriorityThreads() ;

	bool hasHighPriorityCpuThreads() ;

	int32_t getNumWriteThreadsOut() ;

	bool m_needsCleanup;
	bool m_initialized;

	// . allow up to THREAD_TYPE_MAX different thread types for now
	// . types are user-defined numbers
	// . each type has a corresponding thread queue
	// . when a thread is done we place a signal on g_loop's sigqueue so
	//   that it will call m_callback w/ m_state
	ThreadQueue m_threadQueues  [ THREAD_TYPE_MAX ];
	int32_t        m_numQueues;

	bool        m_disabled;
};

extern class Threads g_threads;

void ohcrap ( void *state , class ThreadEntry *t ) ;

#endif // GB_THREADS_H
