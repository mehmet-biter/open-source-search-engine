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
	switch (m_msgType) {
		case msg_type_0:
			if (strlen(slot.getExtraInfo())) {
				snprintf(m_description, sizeof(m_description), "get from %s", slot.getExtraInfo());
			} else if (slot.isIncoming()) {
				if (slot.m_readBuf && slot.m_readBufSize > RDBIDOFFSET) {
					uint8_t rdbId = static_cast<uint8_t>(slot.m_readBuf[RDBIDOFFSET]);
					snprintf(m_description, sizeof(m_description), "get from %s", getDbnameFromId(rdbId));
				}
			}
			break;
		case msg_type_1:
			if (strlen(slot.getExtraInfo())) {
				snprintf(m_description, sizeof(m_description), "add to %s", slot.getExtraInfo());
			} else if (slot.isIncoming()) {
				if (slot.m_readBuf) {
					uint8_t rdbId = static_cast<uint8_t>(slot.m_readBuf[0]);
					snprintf(m_description, sizeof(m_description), "add to %s", getDbnameFromId(rdbId));
				}

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
			if (strlen(slot.getExtraInfo())) {
				snprintf(m_description, sizeof(m_description), "get %s", slot.getExtraInfo());
			} else if (slot.isIncoming()) {
				if (slot.m_readBuf && slot.m_readBufSize >= sizeof(Msg13Request)) {
					Msg13Request *r = reinterpret_cast<Msg13Request*>(slot.m_readBuf);
					snprintf(m_description, sizeof(m_description), "get %s", r->m_isRobotsTxt ? "web page" : "robot.txt");
				}
			}
			break;
		case msg_type_1f:
			strcpy(m_description, "get remote log");
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