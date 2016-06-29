// Matt Wells, copyright Mar 2001

// . extensions of errno
// . have 16th bit set to avoid collisions with existing errnos

#ifndef GB_ERRNO_H
#define GB_ERRNO_H

// use our own errno so threads don't fuck with it
extern int g_errno;

const char *mstrerrno ( int errnum ) ;
const char *mstrerror ( int errnum ) ;

// . this is OR'ed into the errno if the errno occured on a remote machine but
//   was passed back to use through a reply (see UdpSlot.cpp/UdpServer.cpp)
// . this was removed because Multicast::gotReply1() was expecting the g_errno
//   remote codes to be the same as local, like ENOTFOUND to be right!!
//#define REMOTE_ERROR_BIT (0x40000000)

enum {
	EUNUSED1 = 0x00008000  ,
	ETRYAGAIN        , // try doing it again
	ECLOSING         , // can't add cuz we're closing the db
	ENOTFOUND        , // can't find in the db
	EHOSTNAMETOOBIG  , // hostname too big
	EOUTOFSOCKETS    , // no more sockets?
	EURLTOOBIG       , // too many chars in url
	EUNUSED2         ,
	EBADREPLYSIZE    , // reply is wrong length
	EBADREPLY        , // something is wrong w/ reply
	EREPLYTOOSMALL   , // reply is too small  32778
	EREQUESTTOOSHORT , // request length too short
	EBADREQUESTSIZE  , // request length not correct 32780
	EBADREQUEST      , // a bad request
	EUNUSED3         ,
	EBADHOSTID       , // someone tried to use a bad hostId
	EBADENGINEER     , // me being lazy
	EUNUSED4         ,
	EUNUSED5         ,
	EUNUSED6         ,
	EBADRDBID        , // bad rdb id
	EBUFTOOSMALL     , // used in SiteRec.cpp
	ECOMPRESSFAILED  , // used in TitleRec.cpp  32790
	EUNCOMPRESSERROR , // used in TitleRec.cpp
	EBADTITLEREC     , // used in TitleRec.cpp
	EUNUSED6A        ,
	EBADLIST         , // used in titledb/Msg23.cpp
	ENODOCID         , // used in titledb/Msg24.cpp
	ENOHOSTS         , // multicast can't find any hosts
	ENOSLOTS         , // multicast can't use more than X slots
	ENOTHREADSLOTS   , // no more room in thread queue
	EUNUSED7         ,
	EUNUSED8         ,
	EURLTOOLONG      ,
	EUNUSED9         ,
	EDOCADULT        , //parser/xml/XmlDoc.cpp
	EDOCBANNED       , 
	EDOCFORCEDELETE  , // doc force deleted
	EDOCURLSPAM      , // url detected as spam/porn
	EUNUSED10        ,
	EUNUSED11        ,
	EUNUSED12        ,
	EUNUSED13        ,
	EDOCBADCONTENTTYPE   ,
	EUNUSED14            ,
	EDOCBADHTTPSTATUS    , 
	EDOCREDIRECTSTOSELF  , 
	EDOCTOOMANYREDIRECTS ,
	EDOCSIMPLIFIEDREDIR  , 
	EDOCBADREDIRECTURL   ,
	EUNUSED15        ,
	EUNUSED16        ,
	EUNUSED17        ,
	EUNUSED18        ,
	EUNUSED19        ,
	EDOCUNCHANGED    ,
	EUNUSED20        ,
	EDOCDUP          ,  
	EDOCDUPWWW       ,
	EUNUSED21        ,
	EDOCDISALLOWED   , //robots.txt disallows this url
	EUNUSED22        ,
	EUNUSED23        ,
	EUNUSED24        ,
	EUNUSED25        ,
	EUNUSED26        ,
	EUNUSED27        ,
	EUNUSED28        ,
	EUNUSED29        ,
	EUNUSED30        ,
	EUNUSED31        ,
	EUNUSED32        ,
	EUNUSED33        ,
	ETOOMANYFILES    , //used by Rdb class when trying to dump
	EQUERYTOOBIG     , //used by parser/query/SimpleQuery.cpp
	EQUERYTRUNCATED  , //used in Msg39.cpp
	ETOOMANYOPERANDS , //used in Query.cpp
	ENOTLOCAL        , //docId is not local (titledb/Msg20.cpp)
	ETCPTIMEDOUT     , //op timed out TcpServer.cpp
	EUDPTIMEDOUT     , //udp reply timed out
	ESOCKETCLOSED    , //device disconnected (POLL_HUP) Loop.cpp
	EBADMIME         , //HttpMime.cpp
	ENOHOSTSFILE     , //Hostdb::init() needs a hosts file
	EUNUSED34        ,
	EURLHASNOIP      , //parser/url/Url2.cpp::hashIp()
	EBADIP           , //parser/url/Url2.cpp::hashIp()
	EMSGTOOBIG       , //msg is too big
	EDNSBAD          , //dns sent us a wierd response code
	EDNSREFUSED      , //dns refused to talk to us
	EDNSDEAD         , //dns is dead
	EDNSTIMEDOUT     , //was just EUDPTIMEDOUT
	ECOLLTOOBIG      , //collection is too long
	EUNUSED35        ,
	ENOPERM          , //permission denied
	ECORRUPTDATA     , //corrupt data
	ENOCOLLREC       , //no collection record
	ESHUTTINGDOWN    , //shutting down the server
	EHOSTDEAD        , // host is dead
	EBADFILE         , //file is bad
	ETOOEARLY        , //need to wait longer
	EFILECLOSED      , //read on closed file?
	ELISTTOOBIG      , //Rdb::addList() calls this
	ECANCELLED       , //transaction was cancelled
	EUNUSED36        ,
	EUNUSED37        ,
	EBADCHARSET      , // Unsupported charset
	ETOOMANYDOWNLOADS, //too many concurrent http downloads
	EUNUSED38        ,
	ELINKLOOP        , //url is repeating path components in a loop
	ENOCACHE         , // document disallows caching
	EREPAIRING       , // we are in repair mode, cannot add data
	EUNUSED39        ,
	EBADURL          ,
	EDOCFILTERED     , // doc is filtered
	ESSLNOTREADY     , // SSl tcpserver is not ready to do HTTPS request
	ERESTRICTEDPAGE  , // spider trying to download /master or /admin page
	EDOCISERRPG      , // Doc is error page
	EUNUSED40        ,
	EINJECTIONSDISABLED        , // injection is disabled
	EUNUSED41        ,
	EDISKSTUCK       ,
	EDOCREPEATSPAMMER,
	EUNUSED42        ,
	EDOCBADSECTIONS  ,
	EUNUSED43        ,
	EUNUSED44        ,
	EBUFOVERFLOW     ,
	EUNUSED45        ,
	EUNUSED46        ,
	EABANDONED       ,
	ECORRUPTHTTPGZIP ,
	EDOCIDCOLLISION  ,
	ESSLERROR        ,
	EPERMDENIED      ,
	EUNUSED47        ,
	EDIFFBOTINTERNALERROR,
	EDIFFBOTMIMEERROR,
	EDIFFBOTBADHTTPSTATUS,
	EHITCRAWLLIMIT,
	EHITPROCESSLIMIT,
	EINTERNALERROR,
	EBADJSONPARSER,
	EFAKEFIRSTIP,
	EBADHOSTSCONF,
	EWAITINGTOSYNCHOSTSCONF,
	EDOCNONCANONICAL,
	ECUSTOMCRAWLMISMATCH, // a crawl request was made with a name that already existed for bulk request (or the other way around)
	EUNUSED48,
	EBADIMG,
	EUNUSED49,
	ETOOMANYPARENS,

	EDIFFBOTUNABLETOAPPLYRULES,
	EDIFFBOTCOULDNOTPARSE,
	EDIFFBOTCOULDNOTDOWNLOAD,
	EDIFFBOTINVALIDAPI,
	EDIFFBOTVERSIONREQ,
	EDIFFBOTEMPTYCONTENT,
	EDIFFBOTREQUESTTIMEDOUT,
	EDIFFBOTURLPROCESSERROR,
	EDIFFBOTTOKENEXPIRED,
	EDIFFBOTUNKNOWNERROR,

	EMISSINGINPUT,
	EPROXYSSLCONNECTFAILED,
	EINLINESECTIONS,
	EREADONLYMODE,
	ENOTITLEREC,
	EQUERYINGDISABLED,
	EUNUSED50,
	EADMININTERFERENCE,
	EDNSERROR        ,
	EUNUSED51,
	EMALFORMEDQUERY,
	ESHARDDOWN
};

#endif // GB_ERRNO_H
