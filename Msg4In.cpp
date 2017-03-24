#include "Msg4In.h"
#include "Parms.h"

#include "UdpServer.h"
#include "Hostdb.h"
#include "Conf.h"
#include "UdpSlot.h"
#include "Rdb.h"
#include "Repair.h"
#include "JobScheduler.h"
#include "PingServer.h"
#include "ip.h"
#include "Mem.h"
#include <sys/stat.h> //stat()
#include <fcntl.h>


#ifdef _VALGRIND_
#include <valgrind/memcheck.h>
#endif

// . TODO: use this instead of spiderrestore.dat
// . call this once for every Msg14 so it can add all at once...
// . make Msg14 add the links before anything else since that uses Msg10
// . also, need to update spiderdb rec for the url in Msg14 using Msg4 too!
// . need to add support for passing in array of lists for Msg14

static bool addMetaList(const char *p, class UdpSlot *slot = NULL);
static void handleRequest4(UdpSlot *slot, int32_t niceness);
static void processMsg4(void *item);

static GbThreadQueue s_msg4IncomingThreadQueue;

// all these parameters should be preset
bool registerMsg4Handler() {
	logTrace( g_conf.m_logTraceMsg4, "BEGIN" );

	// register ourselves with the udp server
	if ( ! g_udpServer.registerHandler ( msg_type_4, handleRequest4 ) ) {
		log(LOG_ERROR,"%s:%s: Could not register with UDP server!", __FILE__, __func__ );
		return false;
	}

	logTrace( g_conf.m_logTraceMsg4, "END - returning true");

	return true;
}

bool initializeMsg4IncomingThread() {
	return s_msg4IncomingThreadQueue.initialize(processMsg4, "process-msg4");
}

void finalizeMsg4IncomingThread() {
	s_msg4IncomingThreadQueue.finalize();
}

// . destroys the slot if false is returned
// . this is registered in Msg4::set() to handle add rdb record msgs
// . seems like we should always send back a reply so we don't leave the
//   requester's slot hanging, unless he can kill it after transmit success???
// . TODO: need we send a reply back on success????
// . NOTE: Must always call g_udpServer::sendReply or sendErrorReply() so
//   read/send bufs can be freed
static void processMsg4(void *item) {
	UdpSlot *slot = static_cast<UdpSlot*>(item);

	logTrace( g_conf.m_logTraceMsg4, "BEGIN" );

	// extract what we read
	char *readBuf     = slot->m_readBuf;

	// this returns false with g_errno set on error
	if (!addMetaList(readBuf, slot)) {
		logError("calling sendErrorReply error='%s'", mstrerror(g_errno));
		g_udpServer.sendErrorReply(slot,g_errno);

		logTrace(g_conf.m_logTraceMsg4, "END - addMetaList returned false. g_errno=%d", g_errno);
		return;
	}

	// good to go
	g_udpServer.sendReply(NULL, 0, NULL, 0, slot);

	logTrace(g_conf.m_logTraceMsg4, "END - OK");
}

static void handleRequest4(UdpSlot *slot, int32_t /*netnice*/) {
	// if we just came up we need to make sure our hosts.conf is in
	// sync with everyone else before accepting this! it might have
	// been the case that the sender thinks our hosts.conf is the same
	// since last time we were up, so it is up to us to check this
	if ( g_pingServer.m_hostsConfInDisagreement ) {
		g_errno = EBADHOSTSCONF;
		logError("call sendErrorReply");
		g_udpServer.sendErrorReply ( slot , g_errno );

		log(LOG_WARN,"%s:%s: END - hostsConfInDisagreement", __FILE__, __func__ );
		return;
	}

	// need to be in sync first
	if ( ! g_pingServer.m_hostsConfInAgreement ) {
		// . if we do not know the sender's hosts.conf crc, wait 4 it
		// . this is 0 if not received yet
		if (!slot->m_host->m_pingInfo.m_hostsConfCRC) {
			g_errno = EWAITINGTOSYNCHOSTSCONF;
			logError("call sendErrorReply");
			g_udpServer.sendErrorReply ( slot , g_errno );

			log(LOG_WARN,"%s:%s: END - EWAITINGTOSYNCHOSTCONF", __FILE__, __func__ );
			return;
		}

		// compare our hosts.conf to sender's otherwise
		if (slot->m_host->m_pingInfo.m_hostsConfCRC != g_hostdb.getCRC()) {
			g_errno = EBADHOSTSCONF;
			logError("call sendErrorReply");
			g_udpServer.sendErrorReply ( slot , g_errno );

			log(LOG_WARN,"%s:%s: END - EBADHOSTSCONF", __FILE__, __func__ );
			return;
		}
	}

	// extract what we read
	char *readBuf     = slot->m_readBuf;
	int32_t  readBufSize = slot->m_readBufSize;

	// must at least have an rdbId
	if (readBufSize < 7) {
		g_errno = EREQUESTTOOSHORT;
		logError("call sendErrorReply");
		g_udpServer.sendErrorReply ( slot , g_errno );

		log(LOG_ERROR,"%s:%s: END - EREQUESTTOOSHORT", __FILE__, __func__ );
		return;
	}


	// get total buf used
	int32_t used = *(int32_t *)readBuf; //p += 4;

	// sanity check
	if ( used != readBufSize ) {
		// if we send back a g_errno then multicast retries forever
		// so just absorb it!
		logError("msg4: got corrupted request from hostid %" PRId32" used [%" PRId32"] != readBufSize [%" PRId32"]",
		         slot->m_host->m_hostId, used, readBufSize);

		loghex(LOG_ERROR, readBuf, (readBufSize < 160 ? readBufSize : 160), "readBuf (first max. 160 bytes)");

		g_udpServer.sendReply(NULL, 0, NULL, 0, slot);
		//g_udpServer.sendErrorReply(slot,ECORRUPTDATA);return;}

		logError("END");
		return;
	}

	// if we did not sync our parms up yet with host 0, wait...
	if ( g_hostdb.m_hostId != 0 && ! g_parms.inSyncWithHost0() ) {
		// limit logging to once per second
		static int32_t s_lastTime = 0;
		int32_t now = getTimeLocal();
		if ( now - s_lastTime >= 1 ) {
			s_lastTime = now;
			log(LOG_INFO, "msg4: waiting to sync with host #0 before accepting data");
		}
		// tell send to try again shortly
		g_errno = ETRYAGAIN;
		logError("call sendErrorReply");
		g_udpServer.sendErrorReply(slot,g_errno);

		logTrace( g_conf.m_logTraceMsg4, "END - ETRYAGAIN. Waiting to sync with host #0" );
		return;
	}

	/// @todo ALC enable threading when we have made dependency thread-safe
	//s_msg4IncomingThreadQueue.addItem(slot);
	processMsg4(slot);
}


// . Syncdb.cpp will call this after it has received checkoff keys from
//   all the alive hosts for this zid/sid
// . returns false and sets g_errno on error, returns true otherwise
static bool addMetaList(const char *p, UdpSlot *slot) {
	logDebug(g_conf.m_logDebugSpider, "syncdb: calling addMetalist zid=%" PRIu64, *(int64_t *) (p + 4));

	// get total buf used
	int32_t used = *(int32_t *)p;
	// the end
	const char *pend = p + used;
	// skip the used amount
	p += 4;
	// skip zid
	p += 8;

	Rdb  *rdb       = NULL;
	char  lastRdbId = -1;

	/// @note we can have multiple meta list here

	// check if we have enough room for the whole request
	std::map<rdbid_t, std::pair<int32_t, int32_t>> rdbRecSizes;

	const char *pstart = p;
	while (p < pend) {
		// extract rdbId, recSize
		p += sizeof(collnum_t);
		rdbid_t rdbId = static_cast<rdbid_t>(*(char *)p);
		p += 1;
		int32_t recSize = *(int32_t *)p;
		p += 4;

		// . get the rdb to which it belongs, use Msg0::getRdb()
		// . do not call this for every rec if we do not have to
		if (rdbId != lastRdbId || !rdb) {
			rdb = getRdbFromId(rdbId);

			if (!rdb) {
				log(LOG_WARN, "msg4: rdbId of %" PRId32" unrecognized from hostip=%s. dropping WHOLE request",
				    (int32_t)rdbId, slot ? iptoa(slot->getIp()) : "unknown");
				g_errno = ETRYAGAIN;
				return false;
			}

			// an uninitialized secondary rdb? it will have a keysize
			// of 0 if its never been intialized from the repair page.
			// don't core any more, we probably restarted this shard
			// and it needs to wait for host #0 to syncs its
			// g_conf.m_repairingEnabled to '1' so it can start its
			// Repair.cpp repairWrapper() loop and init the secondary
			// rdbs so "rdb" here won't be NULL any more.
			if (rdb->getKeySize() <= 0) {
				time_t currentTime = getTime();
				static time_t s_lastTime = 0;
				if (currentTime > s_lastTime + 10) {
					s_lastTime = currentTime;
					log(LOG_WARN, "msg4: oops. got an rdbId key for a secondary "
							"rdb and not in repair mode. waiting to be in repair mode.");
					g_errno = ETRYAGAIN;
					return false;
				}
			}
		}

		// sanity check
		if (p + recSize > pend) {
			g_errno = ECORRUPTDATA;
			return false;
		}

		auto &item = rdbRecSizes[rdbId];
		item.first += 1;

		int32_t dataSize = recSize - rdb->getKeySize();
		if (rdb->getFixedDataSize() == -1) {
			dataSize -= 4;
		}

		item.second += dataSize;

		// reset g_errno
		g_errno = 0;

		// advance over the rec data to point to next entry
		p += recSize;
	}

	bool hasRoom = true;
	bool anyCantAdd = false;
	for (auto item : rdbRecSizes) {
		Rdb *rdb = getRdbFromId(item.first);
		if (!rdb->hasRoom(item.second.first, item.second.second)) {
			rdb->dumpTree();
			hasRoom = false;
		}
		if(!rdb->canAdd()) {
			anyCantAdd = true;
		}
	}

	if (!hasRoom) {
		logDebug(g_conf.m_logDebugSpider, "One or more target Rdbs  don't have room currently. Returning try-again for this Msg4");
		g_errno = ETRYAGAIN;
		return false;
	}
	if(anyCantAdd) {
		logDebug(g_conf.m_logDebugSpider, "One or more target Rdbs can't currently be added to. Returning try-again for this Msg4");
		g_errno = ETRYAGAIN;
		return false;
	}

	/// @todo ALC we can probably improve performance by preprocessing records in previous loop

	// reset p to before 'check' loop
	p = pstart;

	// . this request consists of multiple recs, so add each one
	// . collnum(2bytes)/rdbId(1byte)/recSize(4bytes)/recData/...
	while (p < pend) {
		// extract collnum, rdbId, recSize
		collnum_t collnum = *(collnum_t *)p;
		p += sizeof(collnum_t);
		rdbid_t rdbId = static_cast<rdbid_t>(*(char *)p);
		p += 1;
		int32_t recSize = *(int32_t *)p;
		p += 4;

		// . get the rdb to which it belongs, use Msg0::getRdb()
		// . do not call this for every rec if we do not have to
		if (rdbId != lastRdbId || !rdb) {
			rdb = getRdbFromId(rdbId);

			if (!rdb) {
				log(LOG_WARN, "msg4: rdbId of %" PRId32" unrecognized from hostip=%s. dropping WHOLE request",
				    (int32_t)rdbId, slot ? iptoa(slot->getIp()) : "unknown");
				g_errno = ETRYAGAIN;
				return false;
			}
		}

		// reset g_errno
		g_errno = 0;

		// . make a list from this data
		// . skip over the first 4 bytes which is the rdbId
		// . TODO: embed the rdbId in the msgtype or something...
		RdbList list;

		// set the list
		// todo: dodgy cast to char*. RdbList should be fixed
		list.set((char *)p, recSize, (char *)p, recSize, rdb->getFixedDataSize(), false, rdb->useHalfKeys(), rdb->getKeySize());

		// advance over the rec data to point to next entry
		p += recSize;

		// keep track of stats
		rdb->readRequestAdd(recSize);

		// this returns false and sets g_errno on error
		bool status = rdb->addListNoSpaceCheck(collnum, &list);

		// bad coll #? ignore it. common when deleting and resetting
		// collections using crawlbot. but there are other recs in this
		// list from different collections, so do not abandon the whole
		// meta list!! otherwise we lose data!!
		if (g_errno == ENOCOLLREC && !status) {
			g_errno = 0;
			status = true;
		}

		if (!status) {
			break;
		}

		// do the next record here if there is one
	}

	// no memory means to try again
	if ( g_errno == ENOMEM ) g_errno = ETRYAGAIN;
	// doing a full rebuid will add collections
	if ( g_errno == ENOCOLLREC  && g_repairMode > 0       )
		g_errno = ETRYAGAIN;

	// are we done
	if ( g_errno ) return false;

	//Initiate dumps for any Rdbs wanting it
	for (auto item : rdbRecSizes) {
		Rdb *rdb = getRdbFromId(item.first);
		if(!rdb->isDumping() && rdb->needsDump()) {
			logDebug(g_conf.m_logDebugSpider, "Rdb %d needs dumping", item.first);
			rdb->dumpTree();
			//we ignore the return value because we have processed the list/msg4
		}
	}

	// success
	return true;
}
