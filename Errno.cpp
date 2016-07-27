#include "gb-include.h"
#include "Errno.h"
#include <string>
#include <pthread.h>

static pthread_key_t s_g_errno_key;

extern "C" {
static void g_errno_destroy(void *key) {
	int *gb_errno = static_cast<int *>(key);
	free(gb_errno);
}
}

void g_errno_init() {
	static bool s_init = false;
	if (!s_init) {
		s_init = true;
		pthread_key_create(&s_g_errno_key, g_errno_destroy);
	}
}

int* g_errno_location() {
	int *gb_errno = static_cast<int*>(pthread_getspecific(s_g_errno_key));
	if (!gb_errno) {
		gb_errno = static_cast<int*>(malloc(sizeof(*gb_errno)));
		pthread_setspecific(s_g_errno_key, gb_errno);
	}
}

const char *mstrerror ( int errnum ) {
	if ( errnum >= GB_ERRNO_BEGIN ) {
		switch ( errnum ) {
			case ETRYAGAIN:
				return "Try doing it again";
			case ECLOSING:
				return "Add denied, db is closing";
			case ENOTFOUND:
				return "Record not found";
			case EHOSTNAMETOOBIG:
				return "Hostname too big";
			case EOUTOFSOCKETS:
				return "No more sockets";
			case EURLTOOBIG:
				return "Too many chars in url";
			case EBADREPLYSIZE:
				return "Reply is wrong length";
			case EBADREPLY        :
				return "Something is wrong with reply";
			case EREPLYTOOSMALL   :
				return "Reply is too small";
			case EREQUESTTOOSHORT :
				return "Request length too short";
			case EBADREQUESTSIZE  :
				return "Request length not correct";
			case EBADREQUEST      :
				return "Bad request";
			case EBADHOSTID       :
				return "Someone tried to use a bad hostId";
			case EBADENGINEER     :
				return "Bad engineer";
			case EBADRDBID        :
				return "Bad Rdb id";
			case EBUFTOOSMALL     :
				return "Buf too small";
			case ECOMPRESSFAILED  :
				return "Compress failed";
			case EUNCOMPRESSERROR :
				return "Uncompress failed";
			case EBADTITLEREC     :
				return "Bad cached document";
			case EBADLIST         :
				return "Bad list";
			case ENODOCID         :
				return "No docid";
			case ENOHOSTS         :
				return "Multicast can not find any hosts";
			case ENOSLOTS         :
				return "No udp slots available";
			case ENOTHREADSLOTS   :
				return "No room for thread in thread queue";
			case EURLTOOLONG      :
				return "Url too long";
			case EDOCADULT        :
				return "Doc adult";
			case EDOCBANNED       :
				return "Doc banned";
			case EDOCFORCEDELETE  :
				return "Doc force deleted";
			case EDOCURLSPAM      :
				return "Url detected as spam or porn";
			case EDOCBADCONTENTTYPE :
				return "Doc bad content type";
			case EDOCBADHTTPSTATUS :
				return "Doc bad http status";
			case EDOCREDIRECTSTOSELF:
				return "Doc redirects to self";
			case EDOCTOOMANYREDIRECTS:
				return "Doc redirected too much";
			case EDOCSIMPLIFIEDREDIR :
				return "Doc redirected to simpler url";
			case EDOCBADREDIRECTURL  :
				return "Doc bad redirect url";
			case EDOCUNCHANGED    :
				return "Doc unchanged";
			case EDOCDUP          :
				return "Doc is a dup";
			case EDOCDUPWWW       :
				return "Doc is dup of a www url";
			case EDOCDISALLOWED   :
				return "robots.txt disallows this url";
			case ETOOMANYFILES    :
				return "Too many files already";
			case EQUERYTOOBIG     :
				return "Query too big";
			case EQUERYTRUNCATED  :
				return "Query was truncated";
			case ENOTLOCAL        :
				return "DocId is not local";
			case ETCPTIMEDOUT     :
				return "Tcp operation timed out";
			case EUDPTIMEDOUT     :
				return "Udp reply timed out";
			case ESOCKETCLOSED    :
				return "Device disconnected (POLL_HUP)";
			case EBADMIME         :
				return "Bad mime";
			case ENOHOSTSFILE     :
				return "No hosts.conf file";
			case EBADIP           :
				return "Bad IP";
			case EMSGTOOBIG       :
				return "Msg is too big";
			case EDNSBAD          :
				return "DNS sent an unknown response code";
			case EDNSREFUSED      :
				return "DNS refused to talk";
			case EDNSDEAD         :
				return "DNS hostname does not exist";
			case EDNSTIMEDOUT     :
				return "DNS timed out";
			case ECOLLTOOBIG      :
				return "Collection is too long";
			case ENOPERM          :
				return "Permission Denied";
			case ECORRUPTDATA     :
				return "Corrupt data";
			case ENOCOLLREC       :
				return "No collection record";
			case ESHUTTINGDOWN    :
				return "Shutting down the server";
			case EHOSTDEAD        :
				return "Host is marked as dead";
			case EBADFILE         :
				return "File is bad";
			case EFILECLOSED      :
				return "Read on closed file";//close on our thread
			case ELISTTOOBIG      :
				return "List is too big";
			case ECANCELLED       :
				return "Transaction was cancelled";
			case EBADCHARSET      :
				return "Unsupported charset";
			case ETOOMANYDOWNLOADS :
				return "Too many outstanding http downloads";
			case ELINKLOOP        :
				return "Url is repeating path components";
			case ENOCACHE         :
				return "Page disallows caching";
			case EREPAIRING       :
				return "Can not add data to host in repair mode";
			case EBADURL          :
				return "Malformed url";
			case EDOCFILTERED     :
				return "Doc is filtered by URL filters";
			case ESSLNOTREADY     :
				return "SSL tcpserver not ready";
			case ERESTRICTEDPAGE  :
				return "Page is /admin or /master and restricted";
			case EDOCISERRPG      :
				return "Doc is error page";
			case EINJECTIONSDISABLED:
				return "Injection is disabled in Master Controls";
			case EDOCREPEATSPAMMER:
				return "Doc is repetitive spam";
			case EDOCBADSECTIONS  :
				return "Doc has malformed sections";
			case EBUFOVERFLOW     :
				return "Static buffer overflow";
			case EABANDONED       :
				return "Injected url already indexed";
			case ECORRUPTHTTPGZIP :
				return "Http server returned corrupted gzip";
			case EDOCIDCOLLISION  :
				return "DocId collision in titledb";
			case ESSLERROR        :
				return "SSL error of some kind";
			case EPERMDENIED      :
				return "Permission denied";
			case EINTERNALERROR:
				return "Internal error";
			case EBADJSONPARSER:
				return "Bad JSON parser";
			case EFAKEFIRSTIP:
				return "Fake firstIp";
			case EBADHOSTSCONF:
				return "A hosts.conf is out of sync";
			case EWAITINGTOSYNCHOSTSCONF:
				return "Wait to ensure hosts.conf in sync";
			case EDOCNONCANONICAL:
				return "Url was dup of canonical page";
			case EBADIMG:
				return "Bad image";
			case ETOOMANYPARENS:
				return "Too many nested parentheses in boolean query";
			case EMISSINGINPUT:
				return "Missing required input parms";
			case EPROXYSSLCONNECTFAILED:
				return "SSL tunnel through HTTP proxy failed";
			case EREADONLYMODE:
				return "In read only mode. Failed.";
			case ENOTITLEREC:
				return "No title rec found when recycling content";
			case EQUERYINGDISABLED:
				return "Querying is disabled in the master controls";
			case EADMININTERFERENCE:
				return "Adminstrative interference";
			case EDNSERROR        :
				return "DNS lookup error";
			case EMALFORMEDQUERY:
				return "Malformed query";
			case ESHARDDOWN:
				return "One or more shards are down";
		}
	}

	return strerror ( errnum );
}

#define STRINGIFY(x) #x

static const char* s_errname[] {
	STRINGIFY( ETRYAGAIN ),
	STRINGIFY( ECLOSING ),
	STRINGIFY( ENOTFOUND ),
	STRINGIFY( EHOSTNAMETOOBIG ),
	STRINGIFY( EOUTOFSOCKETS ),
	STRINGIFY( EURLTOOBIG ),
	STRINGIFY( EUNUSED2 ),
	STRINGIFY( EBADREPLYSIZE ),
	STRINGIFY( EBADREPLY ),
	STRINGIFY( EREPLYTOOSMALL ),
	STRINGIFY( EREQUESTTOOSHORT ),
	STRINGIFY( EBADREQUESTSIZE ),
	STRINGIFY( EBADREQUEST ),
	STRINGIFY( EUNUSED3 ),
	STRINGIFY( EBADHOSTID ),
	STRINGIFY( EBADENGINEER ),
	STRINGIFY( EUNUSED4 ),
	STRINGIFY( EUNUSED5 ),
	STRINGIFY( EUNUSED6 ),
	STRINGIFY( EBADRDBID ),
	STRINGIFY( EBUFTOOSMALL ),
	STRINGIFY( ECOMPRESSFAILED ),
	STRINGIFY( EUNCOMPRESSERROR ),
	STRINGIFY( EBADTITLEREC ),
	STRINGIFY( EUNUSED6A ),
	STRINGIFY( EBADLIST ),
	STRINGIFY( ENODOCID ),
	STRINGIFY( ENOHOSTS ),
	STRINGIFY( ENOSLOTS ),
	STRINGIFY( ENOTHREADSLOTS ),
	STRINGIFY( EUNUSED7 ),
	STRINGIFY( EUNUSED8 ),
	STRINGIFY( EURLTOOLONG ),
	STRINGIFY( EUNUSED9 ),
	STRINGIFY( EDOCADULT ),
	STRINGIFY( EDOCBANNED ),
	STRINGIFY( EDOCFORCEDELETE ),
	STRINGIFY( EDOCURLSPAM ),
	STRINGIFY( EUNUSED10 ),
	STRINGIFY( EUNUSED11 ),
	STRINGIFY( EUNUSED12 ),
	STRINGIFY( EUNUSED13 ),
	STRINGIFY( EDOCBADCONTENTTYPE ),
	STRINGIFY( EUNUSED14 ),
	STRINGIFY( EDOCBADHTTPSTATUS ),
	STRINGIFY( EDOCREDIRECTSTOSELF ),
	STRINGIFY( EDOCTOOMANYREDIRECTS ),
	STRINGIFY( EDOCSIMPLIFIEDREDIR ),
	STRINGIFY( EDOCBADREDIRECTURL ),
	STRINGIFY( EUNUSED15 ),
	STRINGIFY( EUNUSED16 ),
	STRINGIFY( EUNUSED17 ),
	STRINGIFY( EUNUSED18 ),
	STRINGIFY( EUNUSED19 ),
	STRINGIFY( EDOCUNCHANGED ),
	STRINGIFY( EUNUSED20 ),
	STRINGIFY( EDOCDUP ),
	STRINGIFY( EDOCDUPWWW ),
	STRINGIFY( EUNUSED21 ),
	STRINGIFY( EDOCDISALLOWED ),
	STRINGIFY( EUNUSED22 ),
	STRINGIFY( EUNUSED23 ),
	STRINGIFY( EUNUSED24 ),
	STRINGIFY( EUNUSED25 ),
	STRINGIFY( EUNUSED26 ),
	STRINGIFY( EUNUSED27 ),
	STRINGIFY( EUNUSED28 ),
	STRINGIFY( EUNUSED29 ),
	STRINGIFY( EUNUSED30 ),
	STRINGIFY( EUNUSED31 ),
	STRINGIFY( EUNUSED32 ),
	STRINGIFY( EUNUSED33 ),
	STRINGIFY( ETOOMANYFILES ),
	STRINGIFY( EQUERYTOOBIG ),
	STRINGIFY( EQUERYTRUNCATED ),
	STRINGIFY( EUNUSED33A ),
	STRINGIFY( ENOTLOCAL ),
	STRINGIFY( ETCPTIMEDOUT ),
	STRINGIFY( EUDPTIMEDOUT ),
	STRINGIFY( ESOCKETCLOSED ),
	STRINGIFY( EBADMIME ),
	STRINGIFY( ENOHOSTSFILE ),
	STRINGIFY( EUNUSED34 ),
	STRINGIFY( EUNUSED34A ),
	STRINGIFY( EBADIP ),
	STRINGIFY( EMSGTOOBIG ),
	STRINGIFY( EDNSBAD ),
	STRINGIFY( EDNSREFUSED ),
	STRINGIFY( EDNSDEAD ),
	STRINGIFY( EDNSTIMEDOUT ),
	STRINGIFY( ECOLLTOOBIG ),
	STRINGIFY( EUNUSED35 ),
	STRINGIFY( ENOPERM ),
	STRINGIFY( ECORRUPTDATA ),
	STRINGIFY( ENOCOLLREC ),
	STRINGIFY( ESHUTTINGDOWN ),
	STRINGIFY( EHOSTDEAD ),
	STRINGIFY( EBADFILE ),
	STRINGIFY( EUNUSED35A ),
	STRINGIFY( EFILECLOSED ),
	STRINGIFY( ELISTTOOBIG ),
	STRINGIFY( ECANCELLED ),
	STRINGIFY( EUNUSED36 ),
	STRINGIFY( EUNUSED37 ),
	STRINGIFY( EBADCHARSET ),
	STRINGIFY( ETOOMANYDOWNLOADS ),
	STRINGIFY( EUNUSED38 ),
	STRINGIFY( ELINKLOOP ),
	STRINGIFY( ENOCACHE ),
	STRINGIFY( EREPAIRING ),
	STRINGIFY( EUNUSED39 ),
	STRINGIFY( EBADURL ),
	STRINGIFY( EDOCFILTERED ),
	STRINGIFY( ESSLNOTREADY ),
	STRINGIFY( ERESTRICTEDPAGE ),
	STRINGIFY( EDOCISERRPG ),
	STRINGIFY( EUNUSED40 ),
	STRINGIFY( EINJECTIONSDISABLED ),
	STRINGIFY( EUNUSED41 ),
	STRINGIFY( EUNUSED41A ),
	STRINGIFY( EDOCREPEATSPAMMER ),
	STRINGIFY( EUNUSED42 ),
	STRINGIFY( EDOCBADSECTIONS ),
	STRINGIFY( EUNUSED43 ),
	STRINGIFY( EUNUSED44 ),
	STRINGIFY( EBUFOVERFLOW ),
	STRINGIFY( EUNUSED45 ),
	STRINGIFY( EUNUSED46 ),
	STRINGIFY( EABANDONED ),
	STRINGIFY( ECORRUPTHTTPGZIP ),
	STRINGIFY( EDOCIDCOLLISION ),
	STRINGIFY( ESSLERROR ),
	STRINGIFY( EPERMDENIED ),
	STRINGIFY( EUNUSED47 ),
	STRINGIFY( EUNUSED47A ),
	STRINGIFY( EUNUSED47B ),
	STRINGIFY( EUNUSED47C ),
	STRINGIFY( EUNUSED47D ),
	STRINGIFY( EUNUSED47E ),
	STRINGIFY( EINTERNALERROR ),
	STRINGIFY( EBADJSONPARSER ),
	STRINGIFY( EFAKEFIRSTIP ),
	STRINGIFY( EBADHOSTSCONF ),
	STRINGIFY( EWAITINGTOSYNCHOSTSCONF ),
	STRINGIFY( EDOCNONCANONICAL ),
	STRINGIFY( EUNUSED48 ),
	STRINGIFY( EUNUSED48A ),
	STRINGIFY( EBADIMG ),
	STRINGIFY( EUNUSED49 ),
	STRINGIFY( ETOOMANYPARENS ),
	STRINGIFY( EUNUSED49A ),
	STRINGIFY( EUNUSED49B ),
	STRINGIFY( EUNUSED49C ),
	STRINGIFY( EUNUSED49D ),
	STRINGIFY( EUNUSED49E ),
	STRINGIFY( EUNUSED49F ),
	STRINGIFY( EUNUSED49G ),
	STRINGIFY( EUNUSED49H ),
	STRINGIFY( EUNUSED49I ),
	STRINGIFY( EUNUSED49J ),
	STRINGIFY( EMISSINGINPUT ),
	STRINGIFY( EPROXYSSLCONNECTFAILED ),
	STRINGIFY( EUNUSED49K ),
	STRINGIFY( EREADONLYMODE ),
	STRINGIFY( ENOTITLEREC ),
	STRINGIFY( EQUERYINGDISABLED ),
	STRINGIFY( EUNUSED50 ),
	STRINGIFY( EADMININTERFERENCE ),
	STRINGIFY( EDNSERROR ),
	STRINGIFY( EUNUSED51 ),
	STRINGIFY( EMALFORMEDQUERY ),
	STRINGIFY( ESHARDDOWN )
};

#undef STRINGIFY

const char* merrname( int errnum ) {
	if ( errnum >= GB_ERRNO_BEGIN ) {
		return s_errname[ errnum - GB_ERRNO_BEGIN - 1 ] ;
	}

	return NULL;
}

