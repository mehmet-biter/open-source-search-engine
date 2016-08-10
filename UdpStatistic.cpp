#include "UdpStatistic.h"
#include "UdpSlot.h"

UdpStatistic::UdpStatistic(const UdpSlot &slot)
	: m_transId(slot.getTransId()),
	  m_ip(slot.getIp()),
	  m_port(slot.getPort()),
	  m_msgType(slot.getMsgType()),
	  m_description(),
	  m_niceness(slot.getNiceness()),
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
	  m_extraInfo(slot.getExtraInfo()) {
	switch (m_msgType) {
		case msg_type_0:
			strcpy(m_description, "get from xxx"); /// @todo
			break;
		case msg_type_1:
			strcpy(m_description, "add to xxx"); /// @todo
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
			strcpy(m_description, "get web page"); /// @todo
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
	}
}