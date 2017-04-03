// Matt Wells, Copyright Apr 2004

#ifndef GB_PINGSERVER_H
#define GB_PINGSERVER_H

#include "gb-include.h"
#include "Hostdb.h"
#include "SafeBuf.h"
#include "repair_mode.h"

class UdpSlot;
class TcpSocket;

class PingServer {

 public:

	// . set up our PingServer
	// . sets itself from g_conf (our configuration class)
	// . returns false on fatal error
	// . gets filename from Conf.h class
	bool init ( );

	// for dealing with pings
	bool registerHandler ( );

	void sendPingsToAll();

	// ping host #i
	void          pingHost      ( Host *h , uint32_t ip , uint16_t port );

	// . send notes to EVERYONE that you are shutting down
	// . when they get one they'll set your ping to DEAD status
	// . returns false if blocked, true otherwise
	bool broadcastShutdownNotes ( bool  sendEmail                ,
				      void   *state                  ,
				      void  (* callback)(void *state));

	// send an email warning that host "h" is dead
	//bool sendEmail ( Host *h );
	bool sendEmail ( Host *h , 
			 const char *errmsg = NULL,
			 bool oom = false ,
			 bool forceIt      = false);

	bool hostsConfInDisagreement() const { return m_hostsConfInDisagreement; }
	int getNumHostsDead() const { return m_numHostsDead; }

	Host *getMinRepairModeHost() const { return m_minRepairModeHost; }

	// . these functions used by Repair.cpp
	// . we do not tally ourselves when computing m_minRepairMode
	int32_t getMinRepairMode() const {
		// is it us?
		if ( g_repairMode < m_minRepairMode ) return g_repairMode;
		// m_minRepairMode could be -1 if uninitialized
		if ( g_hostdb.getNumHosts() != 1    ) return m_minRepairMode;
		return g_repairMode;
	}
	int32_t getMaxRepairMode() const {
		// is it us?
		if ( g_repairMode > m_maxRepairMode ) return g_repairMode;
		// m_maxRepairMode could be -1 if uninitialized
		if ( g_hostdb.getNumHosts() != 1    ) return m_maxRepairMode;
		return g_repairMode;
	}
	// we do not tally ourselves when computing m_numHostsInRepairMode7
	int32_t getMinRepairModeBesides0() const {
		// is it us?
		if ( g_repairMode < m_minRepairModeBesides0 && 
		     g_repairMode != 0 ) return g_repairMode;
		// m_minRepairMode could be -1 if uninitialized
		if ( g_hostdb.getNumHosts() != 1    ) 
			return m_minRepairModeBesides0;
		return g_repairMode;
	}

	void sendEmailMsg ( int32_t *lastTimeStamp , const char *msg ) ;

	void    setMinRepairMode ( Host *h ) ;

private:
	static void gotReplyWrapperP(void *state, UdpSlot *slot);
	static void gotReplyWrapperP2(void *state, UdpSlot *slot);
	static void handleRequest11(UdpSlot *slot , int32_t niceness);
	static void sentEmailWrapper(void *state, TcpSocket *ts);
	static bool sendAdminEmail(Host  *h,
			           const char  *fromAddress,
                                   const char *toAddress,
			           const char  *body,
			           const char  *emailServIp);
	int32_t m_i;

	// broadcast shutdown info
	int32_t    m_numRequests ;
	int32_t    m_numReplies ;
	void   *m_broadcastState ;
	void  (*m_broadcastCallback) ( void *state );

	int32_t    m_numRequests2;
	int32_t    m_numReplies2;
	int32_t    m_maxRequests2;

	int32_t    m_pingSpacer;
	int32_t    m_sleepCallbackRegistrationSequencer; //for generating unique ids for sleep callback registration/deregistration

	// set by setMinRepairMode() function
	int32_t    m_minRepairMode;
	int32_t    m_maxRepairMode;
	int32_t    m_minRepairModeBesides0;
	Host   *m_minRepairModeHost;
	Host   *m_maxRepairModeHost;
	Host   *m_minRepairModeBesides0Host;

	int32_t m_currentPing  ;
	int32_t m_bestPing     ;
	time_t  m_bestPingDate ;

	// some cluster stats
	int32_t m_numHostsWithForeignRecs;
	int32_t m_numHostsDead;
	bool m_hostsConfInAgreement;
	bool m_hostsConfInDisagreement;
};

extern class PingServer g_pingServer;

extern bool g_recoveryMode;
extern int32_t g_recoveryLevel;

#endif // GB_PINGSERVER_H
