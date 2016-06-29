#include "gb-include.h"

#include "Errno.h"

// use our own errno so threads don't fuck with it
int g_errno;

const char *mstrerror ( int errnum ) {
	return mstrerrno ( errnum );
}

const char *mstrerrno ( int errnum ) {

	switch ( errnum ) {
		case 	ETRYAGAIN        : return "Try doing it again";
		case 	ECLOSING         : return "Add denied, db is closing";
		case 	ENOTFOUND        : return "Record not found";
		case 	EHOSTNAMETOOBIG  : return "Hostname too big";
		case 	EOUTOFSOCKETS    : return "No more sockets";
		case 	EURLTOOBIG       : return "Too many chars in url";
		case 	EBADREPLYSIZE    : return "Reply is wrong length";
		case 	EBADREPLY        : return "Something is wrong with reply";
		case 	EREPLYTOOSMALL   : return "Reply is too small";
		case 	EREQUESTTOOSHORT : return "Request length too short";
		case 	EBADREQUESTSIZE  : return "Request length not correct";
		case 	EBADREQUEST      : return "Bad request";
		case 	EBADHOSTID       : return "Someone tried to use a bad hostId";
		case 	EBADENGINEER     : return "Bad engineer";
		case 	EBADRDBID        : return "Bad Rdb id";
		case 	EBUFTOOSMALL     : return "Buf too small";
		case 	ECOMPRESSFAILED  : return "Compress failed";
		case 	EUNCOMPRESSERROR : return "Uncompress failed";
		case 	EBADTITLEREC     : return "Bad cached document";
		case 	EBADLIST         : return "Bad list";
		case 	ENODOCID         : return "No docid";
		case 	ENOHOSTS         : return "Multicast can not find any hosts";
		case 	ENOSLOTS         : return "No udp slots available";
		case	ENOTHREADSLOTS   : return "No room for thread in thread queue";
		case 	EURLTOOLONG      : return "Url too long";
		case 	EDOCADULT        : return "Doc adult";
		case 	EDOCBANNED       : return "Doc banned";
		case 	EDOCFORCEDELETE  : return "Doc force deleted";
		case	EDOCURLSPAM      : return "Url detected as spam or porn";
		case    EDOCBADCONTENTTYPE : return "Doc bad content type";
		case 	EDOCBADHTTPSTATUS : return "Doc bad http status";
		case	EDOCREDIRECTSTOSELF:return "Doc redirects to self";
		case 	EDOCTOOMANYREDIRECTS: return "Doc redirected too much";
		case	EDOCSIMPLIFIEDREDIR : return "Doc redirected to simpler url";
		case	EDOCBADREDIRECTURL  : return "Doc bad redirect url";
		case	EDOCUNCHANGED    : return "Doc unchanged";
		case	EDOCDUP          : return "Doc is a dup";
		case	EDOCDUPWWW       : return "Doc is dup of a www url";
		case	EDOCDISALLOWED   : return "robots.txt disallows this url";
		case	ETOOMANYFILES    : return "Too many files already";
		case 	EQUERYTOOBIG     : return "Query too big";
		case 	EQUERYTRUNCATED  : return "Query was truncated";
		case 	ENOTLOCAL        : return "DocId is not local";
		case 	ETCPTIMEDOUT     : return "Tcp operation timed out";
		case	EUDPTIMEDOUT     : return "Udp reply timed out";
		case 	ESOCKETCLOSED    : return "Device disconnected (POLL_HUP)";
		case 	EBADMIME         : return "Bad mime";
		case 	ENOHOSTSFILE     : return "No hosts.conf file";
		case 	EBADIP           : return "Bad IP";
		case	EMSGTOOBIG       : return "Msg is too big";
		case	EDNSBAD          : return "DNS sent an unknown response code";
		case	EDNSREFUSED      : return "DNS refused to talk";
		case	EDNSDEAD         : return "DNS hostname does not exist";
		case	EDNSTIMEDOUT     : return "DNS timed out";
		case	ECOLLTOOBIG      : return "Collection is too long";
		case	ENOPERM          : return "Permission Denied";
		case	ECORRUPTDATA     : return "Corrupt data";
		case    ENOCOLLREC       : return "No collection record";
		case	ESHUTTINGDOWN    : return "Shutting down the server";
		case    EHOSTDEAD        : return "Host is marked as dead";
		case	EBADFILE         : return "File is bad";
		case	EFILECLOSED      : return "Read on closed file";//close on our thread
		case	ELISTTOOBIG      : return "List is too big";
		case	ECANCELLED       : return "Transaction was cancelled";
		case    EBADCHARSET      : return "Unsupported charset";
		case	ETOOMANYDOWNLOADS : return "Too many outstanding http downloads";
		case    ELINKLOOP        : return "Url is repeating path components";
		case    ENOCACHE         : return "Page disallows caching";
		case    EREPAIRING       : return "Can not add data to host in repair mode";
		case	EBADURL          : return "Malformed url";
		case	EDOCFILTERED     : return "Doc is filtered by URL filters";
		case    ESSLNOTREADY     : return "SSL tcpserver not ready";
		case	ERESTRICTEDPAGE  : return "Page is /admin or /master and restricted";
		case	EDOCISERRPG      : return "Doc is error page";
		case	EINJECTIONSDISABLED: return "Injection is disabled in Master Controls";
		case	EDISKSTUCK       : return "Disk is stuck";
		case    EDOCREPEATSPAMMER: return "Doc is repetitive spam";
		case	EDOCBADSECTIONS  : return "Doc has malformed sections";
		case	EBUFOVERFLOW     : return "Static buffer overflow";
		case	EABANDONED       : return "Injected url already indexed";
		case	ECORRUPTHTTPGZIP : return "Http server returned corrupted gzip";
		case	EDOCIDCOLLISION  : return "DocId collision in titledb";
		case	ESSLERROR        : return "SSL error of some kind";
		case    EPERMDENIED      : return "Permission denied";
		case    EHITCRAWLLIMIT: return "Hit the page download limit";
		case    EHITPROCESSLIMIT: return "Hit the page process limit";
		case    EINTERNALERROR: return "Internal error";
		case	EBADJSONPARSER: return "Bad JSON parser";
		case	EFAKEFIRSTIP: return "Fake firstIp";
		case	EBADHOSTSCONF: return "A hosts.conf is out of sync";
		case    EWAITINGTOSYNCHOSTSCONF: return "Wait to ensure hosts.conf in sync";
		case	EDOCNONCANONICAL: return "Url was dup of canonical page";
		case    ECUSTOMCRAWLMISMATCH: return "Job name/type mismatch. Job name has already been used for a crawl or bulk job.";
		case    EBADIMG: return "Bad image";
		case	ETOOMANYPARENS: return "Too many nested parentheses in boolean query";
		case EMISSINGINPUT: return "Missing required input parms";
		case EPROXYSSLCONNECTFAILED: return "SSL tunnel through HTTP proxy failed";
		case EINLINESECTIONS: return "Error generating section votes";
		case EREADONLYMODE: return "In read only mode. Failed.";
		case ENOTITLEREC: return "No title rec found when recycling content";
		case EQUERYINGDISABLED: return "Querying is disabled in the master controls";
		case EADMININTERFERENCE: return "Adminstrative interference";
		case	EDNSERROR        : return "DNS lookup error";
		case EMALFORMEDQUERY: return "Malformed query";
		case ESHARDDOWN: return "One or more shards are down";
	}
	// if the remote error bit is clear it must be a regulare errno
	//if ( ! ( errnum & REMOTE_ERROR_BIT ) ) return strerror ( errnum );
	// otherwise, try it with it cleared
	//return mstrerrno ( errnum & (~REMOTE_ERROR_BIT) );
	return strerror ( errnum );
}
