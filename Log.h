// Matt Wells, copyright Feb 2001

// . a great way to record errors encountered during
// . we store the errorMsg, it's length, the type of message and time.
// . when our buf gets full we dump half the messages to the log file (if any)
// . netLogdb can send error msgs for you with it's sendError cmd
// . sendError ( UdpSlot *slot , char *errFormat , ...); (also logs it)

#ifndef GB_LOG_H
#define GB_LOG_H

#include <stdarg.h>
#include <stdint.h>


// THE TYPES OF LOG MESSAGES
// logs information pertaining to more complicated procedures, like
// the merging and dumping of data for the "db" component, or what urls are 
// being spidered for the "build" component.
#define LOG_INFO     0x0001

// the default log message type. also logs slow performance.
#define LOG_WARN     0x0004

// the default log message type. also logs slow performance.
#define LOG_ERROR    0x0008

// programmer error. sanity check. always on.
#define LOG_LOGIC    0x0010  

// Reminders to fix the code. generally disabled.
#define LOG_REMIND   0x0020  
// for debugging. generally disabled.
#define LOG_DEBUG    0x0040  

// for tracing. generally disabled. Enabled for specific code
// sections through config UI
#define LOG_TRACE    0x0080

// times various subroutines for debugging performance.
#define LOG_TIMING   0x0100

// initialization (and shutdown) information. also print routines. always on.
#define LOG_INIT     0x0400

// if a url or link gets truncated, uses this in the "build" context. (Url.cpp
// and Links.cpp)
// also used if a document not added due to quota breech. (Msg16.cpp)
// also used if too many nested tags to parse doc correctly (Xml.cpp)
// in the "query" context for serps too big to be cached. (Msg17.cpp)
#define LOG_LIMIT    0x2000  


// It is convenient to divide everything into components and allow the admin
// to toggle logging for various aspects, such as performance or timing
// messages, of these components:

// addurls related to adding urls
// admin   related to administrative things, sync file, collections
// build   related to indexing (high level)
// conf    configuration issues
// disk    disk reads and writes
// dns     dns networking
// http    http networking
// loop
// net     network later: multicast pingserver. sits atop udpserver.
// query   related to querying (high level)
// rdb     generic rdb things
// spcache related to determining what urls to spider next
// speller query spell checking
// thread  calling threads
// udp     udp networking

// example log:
//456456454 0 INIT         Gigablast Version 1.234
//454544444 0 INIT  thread Allocated 435333 bytes for thread stacks.
//123456789 0 WARN  mem    Failed to alloc 360000 bytes. 
//123456789 0 WARN  query  Failed to intersect lists. Out of memory.
//123456789 0 WARN  query  Too many words. Query truncated.
//234234324 0 REQST http   1.2.3.4 GET /index.html User-Agent
//234234324 0 REPLY http   1.2.3.4 sent 34536 bytes
//345989494 0 REQST build  GET http://hohum.com/foobar.html 
//345989494 0 INFO  build  http://hohum.com/foobar.html ip=4.5.6.7 : Success
//324234324 0 DEBUG build  Skipping xxx.com, would hammer IP.

#define MAX_LOG_MSGS  1024 // in memory

// may also syslog and fprintf the msg.
// ALWAYS returns FALSE (i.e. 0)!!!! so you can say return log.log(...)
void log ( int32_t type , const char *formatString , ... )
	__attribute__ ((format(printf, 2, 3)));

// this defaults to type of LOG_WARN
void log ( const char *formatString , ... )
	__attribute__ ((format(printf, 1, 2)));

// force it to be logged, even if off on log controls panel
void logf ( int32_t type , const char *formatString , ... )
	__attribute__ ((format(printf, 2, 3)));

void loghex( int32_t type, void const *data, const unsigned int len, const char *formatString , ...)
	__attribute__ ((format(printf, 4, 5)));

#define logError(msg, ...) \
	logf(LOG_ERROR, "%s:%s:%d: " msg, __FILE__, __func__, __LINE__, ##__VA_ARGS__);

#define logDebug(condition, ...) \
    if (condition) { \
        logf(LOG_DEBUG, __VA_ARGS__); \
    }

#define logTrace(condition, msg, ...) \
	if (condition) { \
		logf(LOG_TRACE, "%s:%s:%d: " msg, __FILE__, __func__, __LINE__, ##__VA_ARGS__); \
	}

class Log { 

 public:

	// returns true if opened log file successfully, otherwise false
	bool init ( const char *filename );

	// . log this msg
	// . "msg" must be NULL terminated
	// . now is the time of day in milliseconds since the epoch
	// . if "now" is 0 we insert the timestamp for you
	// . if "asterisk" is true we print an asterisk to indicate that
	//   the msg was actually logged earlier but only printed now because
	//   we were in a signal handler at the time
	bool logR ( int64_t now, int32_t type, const char *msg, bool forced = false );

	// returns false if msg should not be logged, true if it should
	bool shouldLog ( int32_t type , const char *msg ) ;

	// just initialize with no file
	Log () ;
	~Log () ;

	void reset ( );

	// save before exiting
	void close () { }

	bool          m_disabled;

	bool m_logTimestamps;
	bool m_logReadableTimestamps;

 private:

	const char *m_filename;
	int     m_fd;

	int64_t m_logFileSize;
	bool makeNewLogFile ( );
};

extern class Log g_log;

#endif // GB_LOG_H
