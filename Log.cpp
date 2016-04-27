#include "gb-include.h"

#include "Mem.h"
#if defined(__linux__)
#include <sys/types.h>
#include <sys/syscall.h>
#endif
#include "Loop.h"
#include "Conf.h"
#include "Process.h"

// a global class extern'd in Log.h
Log g_log;

#include <pthread.h>
// the thread lock
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;


Log::Log () { 
	m_fd = -1; 
	m_filename = NULL; 
	m_needsPrinting = false; 
	m_disabled = false;
	m_logTimestamps = true;
	m_logReadableTimestamps = true;
}

Log::~Log () { 
	//reset(); }
	if ( m_fd >= 0 ) ::close ( m_fd );
}	

// close file if it's open
void Log::reset ( ) {
	// comment this out otherwise we dont log the memleaks in Mem.cpp!!
	//if ( m_fd >= 0 ) ::close ( m_fd );
}

// for example, RENAME log000 to log000-20131104-181932
static bool renameCurrentLogFile ( ) {
	File f;
	char tmp[16];
	sprintf(tmp,"log%03"INT32"",g_hostdb.m_hostId);
	f.set ( g_hostdb.m_dir , tmp );
	// make new filename like log000-bak20131104-181932
	time_t now = getTimeLocal();
	tm *tm1 = gmtime((const time_t *)&now);
	char tmp2[64];
	strftime(tmp2,64,"%Y%m%d-%H%M%S",tm1);
	SafeBuf newName;
	if ( ! newName.safePrintf ( "%slog%03"INT32"-bak%s",
				    g_hostdb.m_dir,
				    g_hostdb.m_hostId,
				    tmp2 ) ) {
		fprintf(stderr,"log rename failed\n");
		return false;
	}
	// rename log000 to log000-2013_11_04-18:19:32
	if ( f.doesExist() ) {
		//fprintf(stdout,"renaming file\n");
		f.rename ( newName.getBufStart() );
	}
	return true;
}


bool Log::init ( const char *filename ) {
	// init these
	m_numErrors =  0;
	m_fd        = -1;
	m_disabled  = false;

	// is there a filename to log our errors to?
	m_filename = filename;
	if ( ! m_filename ) return true;

	//
	// RENAME log000 to log000-20131104-181932
	//
	if ( g_conf.m_logToFile ) {
		// returns false on error
		if ( ! renameCurrentLogFile() ) return false;
	}

	// get size of current file. getFileSize() is defined in File.h.
	m_logFileSize = getFileSize ( m_filename );

	if ( strcmp(m_filename,"/dev/stderr") == 0 ) {
		m_fd = STDERR_FILENO; // 2; // stderr
		return true;
	}

	// open it for appending.
	// create with -rw-rw-r-- permissions if it's not there.
	m_fd = open ( m_filename , 
		      O_APPEND | O_CREAT | O_RDWR ,
		      getFileCreationFlags() );
		      // S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH );
	if ( m_fd >= 0 ) return true;
	// bitch to stderr and return false on error
	fprintf(stderr,"could not open log file %s for appending\n",
		m_filename);
	return false;
}


static const char *getTypeString ( int32_t type ) ;

const char *getTypeString ( int32_t type ) {
	switch ( type ) {
	case LOG_INFO  : return "INF";
	case LOG_WARN  : return "WRN";
	case LOG_ERROR : return "ERR";
	case LOG_TRACE : return "TRC";
	case LOG_LOGIC : return "LOG";
	case LOG_REMIND: return "REM";
	case LOG_DEBUG : return "DBG";
	case LOG_TIMING: return "TIM";
	case LOG_INIT  : return "INI";
	case LOG_LIMIT : return "LIM";
	default: return "UNK";
	}
}


#define MAX_LINE_LEN 20048

bool Log::shouldLog ( int32_t type , const char *msg ) {
	// always log errors/warnings/logic/trace (if trace gets through to here, it is enabled)
	if ( (type == LOG_WARN) || (type == LOG_LOGIC) || (type == LOG_ERROR) || (type == LOG_TRACE)) {
		return true;
	}

	if ( type == LOG_INFO ) return g_conf.m_logInfo;

	if ( type == LOG_LIMIT  ) return g_conf.m_logLimits;
	if ( type == LOG_REMIND ) return g_conf.m_logReminders ;
	if ( type == LOG_TIMING ) {
		if ( msg[0] == 'a' && msg[2] == 'd' )
			return g_conf.m_logTimingAddurl;
		if ( msg[0] == 'a' && msg[2] == 'm' )
			return g_conf.m_logTimingAdmin;
		if ( msg[0] == 'b' ) return g_conf.m_logTimingBuild;
		if ( msg[0] == 'd' ) return g_conf.m_logTimingDb;
		if ( msg[0] == 'n' ) return g_conf.m_logTimingNet;
		if ( msg[0] == 'q' ) return g_conf.m_logTimingQuery;
		if ( msg[0] == 'r' ) return g_conf.m_logTimingRobots;
		if ( msg[0] == 's' ) return g_conf.m_logTimingSpcache;
		return false;
	}
	if ( type != LOG_DEBUG ) return true;
		
	if (msg[0]=='a'&&msg[2]=='d' ) return g_conf.m_logDebugAddurl ;
	if (msg[0]=='a'&&msg[2]=='m' ) return g_conf.m_logDebugAdmin  ;
	if (msg[0]=='b'&&msg[1]=='u' ) return g_conf.m_logDebugBuild  ;
	if (msg[0]=='d'&&msg[1]=='b' ) return g_conf.m_logDebugDb     ;
	if (msg[0]=='d'&&msg[1]=='i' ) return g_conf.m_logDebugDisk   ;
	if (msg[0]=='d'&&msg[1]=='n' ) return g_conf.m_logDebugDns    ;
	if (msg[0]=='d'&&msg[1]=='o' ) return g_conf.m_logDebugDownloads;
	if (msg[0]=='h'&&msg[1]=='t' ) return g_conf.m_logDebugHttp   ;
	if (msg[0]=='i'&&msg[1]=='m' ) return g_conf.m_logDebugImage  ;
	if (msg[0]=='l'&&msg[1]=='o' ) return g_conf.m_logDebugLoop   ;
	if (msg[0]=='l'&&msg[1]=='a' ) return g_conf.m_logDebugLang   ;
	if (msg[0]=='m'&&msg[2]=='m' ) return g_conf.m_logDebugMem    ;
	if (msg[0]=='m'&&msg[2]=='r' ) return g_conf.m_logDebugMerge  ;
	if (msg[0]=='n'&&msg[1]=='e' ) return g_conf.m_logDebugNet    ;
	if (msg[0]=='q'&&msg[1]=='u'&&msg[2]=='e' ) 
		return g_conf.m_logDebugQuery  ;
	if (msg[0]=='q'&&msg[1]=='u'&&msg[2]=='o' ) 
		return g_conf.m_logDebugQuota  ;
	if (msg[0]=='r'&&msg[1]=='o' ) return g_conf.m_logDebugRobots ;
	if (msg[0]=='s'&&msg[1]=='e' ) return g_conf.m_logDebugSEO;
	if (msg[0]=='s'&&msg[2]=='e' ) return g_conf.m_logDebugSpeller;
	if (msg[0]=='s'&&msg[2]=='a' ) return g_conf.m_logDebugStats  ;
	if (msg[0]=='s'&&msg[1]=='u' ) return g_conf.m_logDebugSummary;
	if (msg[0]=='s'&&msg[2]=='i' ) return g_conf.m_logDebugSpider ;
	if (msg[0]=='t'&&msg[1]=='a' ) return g_conf.m_logDebugTagdb  ;
	if (msg[0]=='t'&&msg[1]=='c' ) return g_conf.m_logDebugTcp    ;
	if (msg[0]=='t'&&msg[1]=='h' ) return g_conf.m_logDebugThread ;
	if (msg[0]=='t'&&msg[1]=='i' ) return g_conf.m_logDebugTitle  ;
	if (msg[0]=='r'&&msg[1]=='e' ) return g_conf.m_logDebugRepair ;
	if (msg[0]=='u'&&msg[1]=='d' ) return g_conf.m_logDebugUdp    ;
	if (msg[0]=='u'&&msg[1]=='n' ) return g_conf.m_logDebugUnicode;
	if (msg[0]=='t'&&msg[1]=='o'&&msg[3]=='D' ) 
		return g_conf.m_logDebugTopDocs;
	if (msg[0]=='d'&&msg[1]=='a' ) return g_conf.m_logDebugDate;
	if (msg[0]=='d'&&msg[1]=='d' ) return g_conf.m_logDebugDetailed;
		
	return true;
}

static bool g_loggingEnabled = true;

// 1GB max log file size
#define MAXLOGFILESIZE 1000000000

bool Log::logR ( int64_t now, int32_t type, const char *msg, bool forced ) {
	if ( ! g_loggingEnabled ) {
		return true;
	}

	// return true if we should not log this
	if ( ! forced && ! shouldLog ( type , msg ) ) {
		return true;
	}

	// get "msg"'s length
	int32_t msgLen = gbstrlen ( msg );

	// lock for threads
	pthread_mutex_lock ( &s_lock );

	// do a timestamp, too. use the time synced with host #0 because
	// it is easier to debug because all log timestamps are in sync.
	if ( now == 0 ) now = gettimeofdayInMillisecondsGlobalNoCore();

	// . skip all logging if power out, we do not want to screw things up
	// . allow logging for 10 seconds after power out though
	if ( ! g_process.m_powerIsOn && now - g_process.m_powerOffTime >10000){
		pthread_mutex_unlock ( &s_lock );
		return false;
	}

	// chop off any spaces at the end of the msg.
	while ( is_wspace_a ( msg [ msgLen - 1 ] ) && msgLen > 0 ) msgLen--;

	// a tmp buffer
	char tt [ MAX_LINE_LEN ];
	char *p    = tt;


	if ( m_logTimestamps ) 
	{
        if( m_logReadableTimestamps )
        {
            time_t now_t = (time_t)(now / 1000);
            struct tm *stm = localtime(&now_t);

            p += sprintf ( p , "%04d%02d%02d-%02d%02d%02d-%03d %04" INT32" ", stm->tm_year+1900,stm->tm_mon+1,stm->tm_mday,stm->tm_hour,stm->tm_min,stm->tm_sec,(int)(now%1000), g_hostdb.m_hostId );
        }
        else
        {
            if ( g_hostdb.m_numHosts <= 999 )
                    p += sprintf ( p , "%" UINT64" %03" INT32" ",
                              now , g_hostdb.m_hostId );
            else if ( g_hostdb.m_numHosts <= 9999 )
                    p += sprintf ( p , "%" UINT64" %04" INT32" ",
                              now , g_hostdb.m_hostId );
            else if ( g_hostdb.m_numHosts <= 99999 )
                    p += sprintf ( p , "%" UINT64" %05" INT32" ",
                              now , g_hostdb.m_hostId );
        }
	}



	// Get thread id. pthread_self instead?
	unsigned tid=(unsigned)syscall(SYS_gettid);
	p += sprintf(p, "%06u ", tid);

	// Log level
	p += sprintf(p, "%s ", getTypeString(type));
	

	// then message itself
	const char *x = msg;
	int32_t avail = (MAX_LINE_LEN) - (p - tt) - 1;
	if ( msgLen > avail ) msgLen = avail;
	if ( *x == ':' ) x++;
	if ( *x == ' ' ) x++;
	strncpy ( p , x , avail );
	// capitalize for consistency. no, makes grepping log msgs harder.
	//if ( is_alpha_a(*p) ) *p = to_upper_a(*p);
	p += gbstrlen(p);
	// back up over spaces
	while ( p[-1] == ' ' ) p--;
	// end in period or ? or !
	//if ( p[-1] != '?' && p[-1] != '.' && p[-1] != '!' )
	//	*p++ = '.';
	*p ='\0';
	// the total length, not including the \0
	int32_t tlen = p - tt;

	// call sprintf, but first make sure we have room in m_buf and in
	// the arrays. who know how much room the sprintf is going to need???
	// NOTE: TODO: this is shaky -- fix it!
	if ( tlen  >= 1024 * 32 ||  m_numErrors  >= MAX_LOG_MSGS){
		// this sets m_bufPtr to 0
		if ( ! dumpLog ( ) ) {
			fprintf(stderr,"Log::log: could not dump to file!\n");
			pthread_mutex_unlock ( &s_lock );
			return false;
		}
	}
	// . filter out nasty chars from the message
	// . replace with ~'s
	char cs;
	char *ttp    = tt;
	char *ttpend = tt + tlen;
	for ( ; ttp < ttpend ; ttp += cs ) {
		cs = getUtf8CharSize ( ttp );
		if ( is_binary_utf8 ( ttp ) ) {
			for ( int32_t k = 0 ; k < cs ; k++ ) *ttp++ = '.';
			// careful not to skip the already skipped bytes
			cs = 0;
			continue;
		}
	}

	// . if filesize would be too big then make a new log file
	// . should make a new m_fd
	if ( m_logFileSize + tlen+1 > MAXLOGFILESIZE && g_conf.m_logToFile )
		makeNewLogFile();

	if ( m_fd >= 0 ) {
		write ( m_fd , tt , tlen );
		write ( m_fd , "\n", 1 );
		m_logFileSize += tlen + 1;
	}
	else {
		// print it out for now
		fprintf ( stderr, "%s\n", tt );
	}



	// set the stuff in the array
	m_errorMsg      [m_numErrors] = msg;
	m_errorMsgLen   [m_numErrors] = msgLen;
	m_errorTime     [m_numErrors] = now;
	m_errorType     [m_numErrors] = type;
	// increase the # of errors
	m_numErrors++;

	// unlock for threads
	pthread_mutex_unlock ( &s_lock );
	return false;
}

bool Log::makeNewLogFile ( ) {
	// prevent deadlock. don't log since we are in the middle of logging.
	// otherwise, safebuf, which is used when renaming files, might
	// call logR().
	g_loggingEnabled = false;

	// . rename old log file like log000 to log000-2013_11_04-18:19:32
	// . returns false on error
	bool status = renameCurrentLogFile();

	// re-enable logging since nothing below should call logR() indirectly
	g_loggingEnabled = true;

	if ( ! status ) return false;

	// close old fd
	if ( m_fd >= 0 ) ::close ( m_fd );
	// invalidate
	m_fd = -1;
	// reset
	m_logFileSize = 0;
	// open it for appending.
	// create with -rw-rw-r-- permissions if it's not there.
	m_fd = open ( m_filename , 
		      O_APPEND | O_CREAT | O_RDWR ,
		      getFileCreationFlags() );
		      // S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH );
	if ( m_fd >= 0 ) return true;
	// bitch to stderr and return false on error
	fprintf(stderr,"could not open new log file %s for appending\n",
		m_filename);
	return false;
}

// keep a special buf
static char  s_buf[1024*64];
static char *s_ptr      = s_buf;
static bool  s_overflow = false;
static char  s_problem  = '\0';

// once we're no longer in a sig handler this is called by Loop.cpp
// if g_log.needsPrinting() is true
void Log::printBuf ( ) {
	// was there a problem?
	if ( s_problem  ) fprintf(stderr,"Log::printBuf: had problem. '%c'\n",
				  s_problem);
	// or overflow?
	if ( s_overflow ) fprintf(stderr,"Log::printBuf: had overflow.\n");
	// point to the buf
	char *p    = s_buf;
	char *pend = s_ptr;
	// reset everything
	s_overflow      = false;
	s_problem       = '\0';
	m_needsPrinting = false;
	// bail if nothing to print, maybe first msg overflowed?
	if ( s_buf == s_ptr ) return;
	// reset buffer here
	s_ptr           = s_buf;
	// now print all log msgs we got while in a signal handler
 loop:
	// sanity check
	if ( p + 8 > pend ) {
		fprintf(stderr,"Had error in Log.cpp: breech of log buffer4.");
		s_ptr = s_buf;
		return;
	}
	// first 4 bytes are the size of the string arguments
	int32_t stringSizes;
	gbmemcpy ( (char *)&stringSizes , p , 4 );
	p += 4;
	// then the type of the msg
	int32_t type;
	gbmemcpy ( (char *)&type , p , 4 );
	p += 4;
	// then the format string
	char *format = p;
	// its length including the \0
	//int32_t flen = gbstrlen ( format ) + 1;
	int32_t flen = 0;
	char *q = format;
	while ( q < pend && *q ) q++;
	if ( q >= pend ) {
		fprintf(stderr,"Had error in Log.cpp: breech of log buffer3.");
		s_ptr = s_buf;
		return;
	}
	flen = q - p + 1;
	p += flen;
	// skip the string arguments now
	p += stringSizes;
	// sanity check
	if ( p + 8 + 4 > pend || p < s_buf ) {
		fprintf(stderr,"Had error in Log.cpp: breech of log buffer2.");
		s_ptr = s_buf;
		return;
	}
	// get time
	int64_t now ;
	gbmemcpy ( (char *)&now , p , 8 );
	p += 8;
	// get size of args
	int32_t apsize ;
	gbmemcpy ( (char *)&apsize , p , 4 );
	p += 4;
	// dword align
	int32_t rem = ((PTRTYPE)p) % 4;
	if ( rem > 0 ) p +=  4 - rem;
	// get va_list... needs to be word aligned!!
	va_list ap ;
	// MDW FIX ME
	//ap = (char *)(void*)p;
	p += apsize;
	// . sanity check
	// . i've seen this happen a lot lately since i started logging cancel
	//   acks perhaps?
	if ( p > pend || p < s_buf ) {
		fprintf(stderr,"Had error in Log.cpp: breech of log buffer.");
		s_ptr = s_buf;
		return;
	}
	// print msg into this buf
	char buf[1024*4];
	// print it into our buf now
	vsnprintf ( buf , 1024*4 , format , ap );
	// pass buf to g_log
	logR ( now , type , buf , true );
	// if not done loop back
	if ( p < pend ) goto loop;
}

// . IMPORTANT: should be called while the lock is on!
// . we just re-write to the file
bool Log::dumpLog ( ) {
	// for now don't dump
	m_numErrors =  0;

	// for now just return true always
	return true;
}

bool log ( int32_t type , const char *formatString , ...) {
	if ( g_log.m_disabled ) return false;

	// do not log it if we should not
	if ( ! g_log.shouldLog ( type , formatString ) ) return false;

	// is it congestion?
	if ( g_errno == ENOSLOTS && ! g_conf.m_logNetCongestion ) return false;

	// this is the argument list (variable list)
	va_list   ap;

	// can we log if we're a sig handler? don't take changes
	// print msg into this buf
	char buf[1024*10];

	// copy the error into the buffer space
	va_start ( ap, formatString);

	// print it into our buf now
	vsnprintf ( buf , 1024*10 , formatString , ap );

	va_end(ap);

	// pass buf to g_log
	g_log.logR ( 0, type, buf );

	// always return false
	return false;
}

bool log ( const char *formatString , ... ) {
	if ( g_log.m_disabled ) return false;

	// do not log it if we should not
	if ( ! g_log.shouldLog ( LOG_WARN , formatString ) ) return false;

	// is it congestion?
	if ( g_errno == ENOSLOTS && ! g_conf.m_logNetCongestion ) return false;

	// this is the argument list (variable list)
	va_list   ap;

	// can we log if we're a sig handler? don't take changes
	// print msg into this buf
	char buf[1024*10];

	// copy the error into the buffer space
	va_start ( ap, formatString);

	// print it into our buf now
	vsnprintf ( buf , 1024*10 , formatString , ap );

	va_end(ap);
	
	// pass buf to g_log
	// ### BR 20151217: Default to DEBUG if no log level given
	/// @todo ALC shouldn't this be LOG_WARN?
	g_log.logR ( 0 , LOG_DEBUG , buf , false );

	// always return false
	return false;
}

bool logf ( int32_t type , const char *formatString , ...) {
	if ( g_log.m_disabled ) return false;

	// is it congestion?
	if ( g_errno == ENOSLOTS && ! g_conf.m_logNetCongestion ) return false;

	// this is the argument list (variable list)
	va_list   ap;

	// can we log if we're a sig handler? don't take changes
	// print msg into this buf
	char buf[1024*10];

	// copy the error into the buffer space
	va_start ( ap, formatString);

	// print it into our buf now
	vsnprintf ( buf , 1024*10 , formatString , ap );

	va_end(ap);

	// pass buf to g_log
	g_log.logR ( 0, type, buf, true );

	// always return false
	return false;
}



static void hexdump(void const *data, const unsigned int len, char *dest, const int dest_len)
{
	unsigned int i;
	unsigned int r,c;


	if (!data || len <= 0)
	{
		return;
	}

	int	dest_used = 0;
	char *destptr = dest;


	char line[80]; // line length is actually 78 + null terminator
	char *lptr;

	for (r=0,i=0; (r<(len/16+(len%16!=0))) && (dest_len - (dest_used+80)>= 0); r++,i+=16)
	{
		lptr = line;
			lptr += sprintf(lptr, "\n%04X:   ",i); 

		for (c=i; c<i+8; c++) /* left half of hex dump */
		{
			if (c<len)
			{
				lptr += sprintf(lptr, "%02X ",((unsigned char const *)data)[c]);
			}
			else
			{
				lptr += sprintf(lptr, "   "); /* pad if short line */
			}
		}

		lptr += sprintf(lptr, "  ");

		
		for (c=i+8; c<i+16; c++) /* right half of hex dump */
		{
			if (c<len)
			{
				lptr += sprintf(lptr, "%02X ",((unsigned char const *)data)[c]);
			}
			else
			{
				lptr += sprintf(lptr, "   "); /* pad if short line */
			}
		}
		
		lptr += sprintf(lptr, "   ");
		
		for (c=i; c<i+16; c++) /* ASCII dump */
		{
			if (c<len)
			{
				if (((unsigned char const *)data)[c]>=32 &&
				    ((unsigned char const *)data)[c]<127)
				{
					lptr += sprintf(lptr, "%c",((char const *)data)[c]);
				}
				else
				{
					lptr += sprintf(lptr, "."); /* put this for non-printables */
				}
			}
			else
			{
				lptr += sprintf(lptr, " "); /* pad if short line */
			}
		}
		
		destptr += sprintf(destptr, "%s", line);
		dest_used = destptr - dest;
	}

	destptr += sprintf(destptr, "\n");
}


bool loghex( int32_t type, void const *data, const unsigned int len, const char *formatString , ...) {
	if ( g_log.m_disabled ) return false;
		
	// do not log it if we should not
	// is it congestion?
	if ( g_errno == ENOSLOTS && ! g_conf.m_logNetCongestion ) return false;
		
	// this is the argument list (variable list)
	va_list   ap;
	// can we log if we're a sig handler? don't take changes
	// print msg into this buf
	char buf[1024*10];
	// copy the error into the buffer space
	va_start ( ap, formatString);
	// print it into our buf now
	vsnprintf ( buf , 1024*10 , formatString , ap );
	va_end(ap);
	
	int written = strlen(buf);
	hexdump(data, len, &buf[written], (1024*10)-written);
	
	// pass buf to g_log
	g_log.logR ( 0 , type , buf );

	// always return false
	return false;
}
