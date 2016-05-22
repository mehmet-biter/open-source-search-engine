#include "gb-include.h"

#include "Conf.h"
#include "Parms.h"
#include "Proxy.h"
#include "Msg3a.h" // MAX_SHARDS

Conf g_conf;

static bool s_setUmask = false;

mode_t getFileCreationFlags() {
	if ( ! s_setUmask ) {
		s_setUmask = true;
		umask  ( 0 );
	}

	return  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH ;
}

mode_t getDirCreationFlags() {
	if ( ! s_setUmask ) {
		s_setUmask = true;
		umask  ( 0 );
	}

	return  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IXUSR | S_IXGRP;
}

Conf::Conf ( ) {
	m_save = true;
	m_doingCommandLine = false;

	// set max mem to 16GB at least until we load on disk
	m_maxMem = 16000000000;
}

static bool isInWhiteSpaceList ( const char *p , const char *buf ) {
	if ( ! p ) return false;

	const char *match = strstr ( buf , p );
	if ( ! match ) return false;
	
	int32_t len = gbstrlen(p);

	// ensure book-ended by whitespace
	if (  match && 
	      (match == buf || is_wspace_a(match[-1])) &&
	      (!match[len] || is_wspace_a(match[len])) )
		return true;

	// no match
	return false;
}

bool Conf::isCollAdmin ( TcpSocket *sock , HttpRequest *hr ) {
	// master always does
	if ( isMasterAdmin ( sock , hr ) ) return true;

	CollectionRec *cr = g_collectiondb.getRec ( hr , true );
	if ( ! cr ) return false;

	return isCollAdmin2 ( sock , hr , cr );

}

bool Conf::isCollAdminForColl ( TcpSocket *sock, HttpRequest *hr, const char *coll ) {
	CollectionRec *cr = g_collectiondb.getRec ( coll );

	if ( ! cr ) return false;

	return isCollAdmin2 ( sock , hr , cr );
}

bool Conf::isCollAdmin2 ( TcpSocket *sock, HttpRequest *hr, CollectionRec *cr ) {
	if ( ! cr ) return false;

	// never for main! must be root!
	if ( strcmp(cr->m_coll,"main")==0 ) return false;

	if ( ! g_conf.m_useCollectionPasswords) return false;

	// empty password field? then allow them through
	if ( cr->m_collectionPasswords.length() <= 0 &&
	     cr->m_collectionIps      .length() <= 0 )
		return true;

	// a good ip?
	const char *p   = iptoa(sock->m_ip);
	char *buf = cr->m_collectionIps.getBufStart();
	if ( isInWhiteSpaceList ( p , buf ) ) return true;

	// if they got the password, let them in
	p = hr->getString("pwd");
	if ( ! p ) p = hr->getString("password");
	if ( ! p ) p = hr->getStringFromCookie("pwd");
	if ( ! p ) return false;
	buf = cr->m_collectionPasswords.getBufStart();
	if ( isInWhiteSpaceList ( p , buf ) ) return true;

	return false;
}
	

// . is user a root administrator?
// . only need to be from root IP *OR* have password, not both
bool Conf::isMasterAdmin ( TcpSocket *socket , HttpRequest *hr ) {
	bool isAdmin = false;

	// totally open access?
	//if ( m_numConnectIps  <= 0 && m_numMasterPwds <= 0 )
	if ( m_connectIps.length() <= 0 &&
	     m_masterPwds.length() <= 0 )
		isAdmin = true;

	// coming from root gets you in
	if ( socket && isMasterIp ( socket->m_ip ) ) 
		isAdmin = true;

	if ( hasMasterPwd ( hr ) ) 
		isAdmin = true;

	if ( ! isAdmin )
		return false;

	// default this to true so if user specifies &admin=0 then it 
	// cancels our admin view
	if ( hr && ! hr->getLong("admin",1) )
		return false;

	return true;
}


bool Conf::hasMasterPwd ( HttpRequest *hr ) {
	if ( m_masterPwds.length() <= 0 )
		return false;

	const char *p = hr->getString("pwd");

	if ( ! p ) p = hr->getString("password");

	if ( ! p ) p = hr->getStringFromCookie("pwd");

	if ( ! p ) return false;

	const char *buf = m_masterPwds.getBufStart();

	return isInWhiteSpaceList ( p , buf );
}

// . check this ip in the list of admin ips
bool Conf::isMasterIp ( uint32_t ip ) {
	if ( m_connectIps.length() <= 0 ) return false;

	char *p = iptoa(ip);
	char *buf = m_connectIps.getBufStart();

	return isInWhiteSpaceList ( p , buf );
}

bool Conf::isConnectIp ( uint32_t ip ) {
	return isMasterIp(ip);
}

// . set all member vars to their default values
void Conf::reset ( ) {
	g_parms.setToDefault ( (char *)this , OBJ_CONF ,NULL);
	m_save = true;
}

bool Conf::init ( char *dir ) { // , int32_t hostId ) {
	g_parms.setToDefault ( (char *)this , OBJ_CONF ,NULL);
	m_save = true;

	char fname[1024];
	File f;

	m_isLocal = false;

	if ( dir ) sprintf ( fname , "%sgb.conf", dir );
	else       sprintf ( fname , "./gb.conf" );

	// try regular gb.conf then
	f.set ( fname );

	// make sure g_mem.maxMem is big enough temporarily
	g_conf.m_maxMem = 8000000000; // 8gb temp

	bool status = g_parms.setFromFile ( this , fname , NULL , OBJ_CONF );

	if ( g_conf.m_maxMem < 10000000 ) g_conf.m_maxMem = 10000000;

	// if not there, create it!
	if ( ! status ) {
		log("gb: Creating %s from defaults.",fname);
		g_errno = 0;
		// set to defaults
		g_conf.reset();
		// and save it
		m_save = true;
		save();
		// clear errors
		g_errno = 0;
		status = true;
	}

	if ( ! g_mem.init ( ) ) return false;

	// always turn this off
	g_conf.m_testMem      = false;

	// and this, in case you forgot to turn it off
	if ( g_conf.m_isLive ) g_conf.m_doConsistencyTesting = false;

	// and this on
	g_conf.m_indexDeletes = true;

	// this off
	g_conf.m_repairingEnabled = false;

	// force on for now
	g_conf.m_useStatsdb = true;

	// hard-code disable this -- could be dangerous
	g_conf.m_bypassValidation = true;

	// this could too! (need this)
	g_conf.m_allowScale = true;//false;

	// . until we fix spell checker
	// . the hosts splitting count isn't right and it just sends to like
	//   host #0 or something...
	g_conf.m_doSpellChecking = false;

	g_conf.m_forceIt = false;

	// sanity check
	if ( g_hostdb.m_indexSplits > MAX_SHARDS ) {
		log("db: Increase MAX_SHARDS");
		char *xx = NULL; *xx = 0; 
	}

	// HACK: set this now
	setRootIps();

	return status;
}

void Conf::setRootIps ( ) {
	// set m_numDns based on Conf::m_dnsIps[] array
	int32_t i; for ( i = 0; i < MAX_DNSIPS ; i++ ) {
		m_dnsPorts[i] = 53;
		if ( ! g_conf.m_dnsIps[i] ) break;
	}
	m_numDns = i;

	Host *h = g_hostdb.getMyHost();

	// fail back to public dns
	const char *ipStr = PUBLICLY_AVAILABLE_DNS1;

	if ( h->m_type & HT_SCPROXY ) ipStr = PUBLICLY_AVAILABLE_DNS1; 
	if ( h->m_type & HT_PROXY ) ipStr = PUBLICLY_AVAILABLE_DNS1; 

	if ( m_numDns == 0 ) {
		m_dnsIps[0] = atoip( ipStr , gbstrlen(ipStr) );
		m_dnsPorts[0] = 53;
		m_numDns = 1;
	}

	// default this to off on startup for now until it works better
	m_askRootNameservers = false;

	// and return as well
	return;
}

// . parameters can be changed on the fly so we must save Conf
bool Conf::save ( ) {
	if ( ! m_save ) {
		return true;
	}

	// always reset this before saving
	bool keep = g_conf.m_testMem ;
	g_conf.m_testMem = false;

	// fix so if we core in malloc/free we can still save conf
	char fnbuf[1024];
	SafeBuf fn(fnbuf,1024);
	fn.safePrintf("%sgb.conf",g_hostdb.m_dir);
	bool status = g_parms.saveToXml ( (char *)this , fn.getBufStart(), OBJ_CONF );

	// restore
	g_conf.m_testMem = keep;
	return status;
}

// . get the default collection based on hostname
//   will look for the hostname in each collection for a match
//   no match defaults to default collection
char *Conf::getDefaultColl ( char *hostname, int32_t hostnameLen ) {
	if ( ! m_defaultColl || ! m_defaultColl[0] ) {
		return "main";
	}

	// just use default coll for now to keep things simple
	return m_defaultColl;
}
