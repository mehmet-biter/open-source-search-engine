#include "Msg56.h"
#include "msgtype_t.h"
#include "UdpServer.h"
#include "UdpSlot.h"
#include "Log.h"
#include "Conf.h"
#include "Hostdb.h"
#include "GbMutex.h"
#include "ScopedLock.h"
#include "GbUtil.h"
#include <pthread.h>
#include <errno.h>
#include <stdio.h>


static void handleRequest56(UdpSlot *slot, int32_t /*niceness*/);


bool registerMsg56Handler() {
	if(!g_udpServer.registerHandler(msg_type_56, handleRequest56)) {
		log(LOG_ERROR,"%s:%s: Could not register with UDP server", __FILE__, __func__);
		return false;
	}

	return true;
}


static void handleRequest56(UdpSlot *slot, int32_t /*niceness*/) {
	if(g_conf.m_logDebugWatchdog)
		log(LOG_DEBUG,"watchdog: got watchdog request. Responding.");
	g_udpServer.sendReply(NULL, 0, NULL, 0, slot);
}



int getEffectiveWatchdogInterval(const Host *host) {
	const Host *myself = g_hostdb.getMyHost();
	//We're not dead.
	if(host==myself)
		return g_conf.m_watchdogInterval;
	//We're in the same shard. spider-hosts sends msg4 to query-hosts, so watchdog is probably not
	//needed. query-host doesn't normally send to spider-host but 600ms-watchtdog is acceptable as long as we stay within the shard
	if(host->m_shardNum==myself->m_shardNum)
		return g_conf.m_watchdogInterval;
	//simple setup
	if(host->m_spiderEnabled && myself->m_spiderEnabled && host->m_queryEnabled && myself->m_queryEnabled)
		return g_conf.m_watchdogInterval;
	//query-only hosts talk a lot to query-only hots for corodinating queries, msg39, msg20 summaries, link-info, ...
	if(!host->m_spiderEnabled && !myself->m_spiderEnabled)
		return g_conf.m_watchdogInterval;
	//spiders talk to spiders for oordinatoing who crawls what, IP-address lookups, etc.
	if(!host->m_queryEnabled && !myself->m_queryEnabled)
		return g_conf.m_watchdogInterval;
	//we rarely talk to that host, if ever.
	return g_conf.m_watchdogInterval * 100;
}



static pthread_t tid;
static bool please_shut_down = false;
static GbMutex mtx;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;


static void msg56ResponseWrapper(void * /*state*/, UdpSlot * slot) {
	if(g_conf.m_logDebugWatchdog) {
		if(slot->getErrno()==0)
			log(LOG_DEBUG,"watchdog: Got watchdog response");
		else
			log(LOG_INFO,"watchdog: Didn't get watchdog response, errno=%d (%s)", slot->getErrno(), mstrerror(slot->getErrno()));
	}
}


static void sendWatchdog(const Host *host, int64_t timeout) {
	if(g_conf.m_logDebugWatchdog)
		log(LOG_DEBUG,"watchdog: Sending watchdog to host %d", host->m_hostId);
	if(!g_udpServer.sendRequest(NULL, 0,             //msg,size
	                            msg_type_56,
		                    host->m_ip, host->m_port, host->m_hostId,
	                            NULL,
	                            (void*)(intptr_t)host->m_hostId, msg56ResponseWrapper,
	                            timeout,
				    0,                     //niceness
				    NULL,                  //extrainfo
				    -1, -1, 0              //backoff, maxwait, maxresends
   				   ))
	{
		log(LOG_ERROR,"watchdog: Could not send to host %d", host->m_hostId);
	}
}

static void sendWatchdogs() {
	uint64_t now = getCurrentTimeNanoseconds();
	const Host *myself = g_hostdb.getMyHost();

	for(int i=0; i<g_hostdb.getNumHosts(); i++) {
		const Host *host = g_hostdb.getHost(i);
		//Awkward and complicated logic follows:
		//  - If we have seen a response from a host recently (less than watchdog_interval) then
		//    that host is obviously alive and there is no need for sending a watchdog message to it
		//  - If it's a host we normally send requests to then we should verify frequently if it's alive
		//  - If it's a host we normally don't send message to then watchdogs should still be sent but
		//    with a much longer interval
		//  - We want to detect when a host is unresponsive within 2 seconds
		//    - That means that the watchdog interval should be around 600ms and a host is declared
		//      unresponsive when 3 watchdogs have not been responded to
		//  - We don't want to pester hosts we don't normally talk to
		//  - It must scale to a large number of instances
		
		if(host==myself) //don't send watchdog to ourselves. We're not dead.
			continue;
		
		if(host->getLastRequestSendTimestamp() < now - getEffectiveWatchdogInterval(host))
			sendWatchdog(host,g_conf.m_watchdogInterval);
	}
}


static void *watchdogThread(void *) {
	ScopedLock sl(mtx);
	while(!please_shut_down) {
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME,&ts);
		ts.tv_sec += g_conf.m_watchdogInterval/1000;
		ts.tv_nsec += (g_conf.m_watchdogInterval%1000)*1000000;
		if(ts.tv_nsec >= 1000000000) {
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000;
		}
		
		int rc = pthread_cond_timedwait(&cond,&mtx.mtx,&ts);
		if(rc==ETIMEDOUT)
			sendWatchdogs();
	}
	return NULL;
}


bool initializeWatchdog() {
	please_shut_down = false;
	int rc = pthread_create(&tid,NULL,watchdogThread,NULL);
	if(rc!=0) {
		log(LOG_ERROR,"watchdog: pthread_create() failed with rc=%d (%s)", rc, strerror(rc));
		return false;
	}
	return true;
}

void finalizeWatchdog() {
	ScopedLock sl(mtx);
	please_shut_down = true;
	pthread_cond_signal(&cond);
	sl.unlock();

	pthread_join(tid,NULL);
}
