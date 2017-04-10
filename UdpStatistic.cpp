#include <cstdio>
#include "UdpStatistic.h"
#include "UdpSlot.h"
#include "Msg13.h"
#include "Msg0.h"
#include "Rdb.h"

UdpStatistic::UdpStatistic(const UdpSlot &slot)
	: m_transId(slot.getTransId()),
	  m_ip(slot.getIp()),
	  m_port(slot.getPort()),
	  m_msgType(slot.getMsgType()),
	  m_description(),
	  m_niceness(slot.getNiceness()),
	  m_convertedNiceness(slot.getConvertedNiceness()),
	  m_numDatagramRead(slot.getNumDgramsRead()),
	  m_numDatagramSent(slot.getNumDgramsSent()),
	  m_numAckRead(slot.getNumAcksRead()),
	  m_numAckSent(slot.getNumAcksSent()),
	  m_numPendingRead(slot.getDatagramsToRead()),
	  m_numPendingSend(slot.getDatagramsToSend()),
	  m_resendCount(slot.getResendCount()),
	  m_timeout(slot.getTimeout()),
	  m_startTime(slot.getStartTime()),
	  m_lastReadTime(slot.getLastReadTime()),
	  m_lastSendTime(slot.getLastSendTime()),
	  m_hasCallback(slot.hasCallback()),
	  m_hasCalledHandler(slot.hasCalledHandler()),
	  m_hasCalledCallback(slot.hasCalledCallback()),
	  m_extraInfo() {
	char *buf;
	int32_t bufSize;
	if (slot.isIncoming()) {
		buf = slot.m_readBuf;
		bufSize = slot.m_readBufSize;
	} else {
		buf = slot.m_sendBuf;
		bufSize = slot.m_sendBufSize;
	}

	switch (m_msgType) {
		case msg_type_0:
			if (buf && bufSize > RDBIDOFFSET) {
				rdbid_t rdbId = static_cast<rdbid_t>(buf[RDBIDOFFSET]);
				snprintf(m_description, sizeof(m_description), "get from %s", getDbnameFromId(rdbId));
			}
			break;
		case msg_type_1:
			if (buf) {
				rdbid_t rdbId = static_cast<rdbid_t>(buf[0]);
				snprintf(m_description, sizeof(m_description), "add to %s", getDbnameFromId(rdbId));
			}
			break;
		case msg_type_4:
			strcpy(m_description, "meta add");
			break;
		case msg_type_7:
			strcpy(m_description, "inject");
			break;
		case msg_type_c:
			strcpy(m_description, "getting ip");
			break;
		case msg_type_11:
			strcpy(m_description, "ping");
			break;
		case msg_type_13:
			if (buf && static_cast<size_t>(bufSize) >= sizeof(Msg13Request)) {
				Msg13Request *r = reinterpret_cast<Msg13Request*>(buf);
				snprintf(m_description, sizeof(m_description), "get %s", r->m_isRobotsTxt ? "web page" : "robot.txt");
			}
			break;
		case msg_type_20:
			strcpy(m_description, "get summary");
			break;
		case msg_type_22:
			strcpy(m_description, "get titlerec");
			break;
		case msg_type_25:
			strcpy(m_description, "get link info");
			break;
		case msg_type_39:
			strcpy(m_description, "get docids");
			break;
		case msg_type_3e:
			strcpy(m_description, "sync parms");
			break;
		case msg_type_3f:
			strcpy(m_description, "update parms");
			break;
		case msg_type_54:
			strcpy(m_description, "proxy spider");
			break;
		case msg_type_56:
			strcpy(m_description, "watchdog");
			break;
		case msg_type_c1:
			strcpy(m_description, "get crawl info");
			break;
		case msg_type_fd:
			strcpy(m_description, "proxy forward");
			break;
		case msg_type_dns:
			strcpy(m_extraInfo, slot.getExtraInfo());
			break;
	}
}

