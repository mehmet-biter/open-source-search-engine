// Matt Wells, Copyright Apr 2004

#ifndef GB_PINGSERVER_H
#define GB_PINGSERVER_H

#include "Hostdb.h"
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

	void sendEmailMsg ( int32_t *lastTimeStamp , const char *msg ) ;

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
};

extern class PingServer g_pingServer;

extern bool g_recoveryMode;
extern int32_t g_recoveryLevel;

#endif // GB_PINGSERVER_H
