#ifndef GB_UDPSTATISTICS_H
#define GB_UDPSTATISTICS_H

#include <stdint.h>
#include <vector>
#include "MsgType.h"

class UdpStatistic {
	const char* getDescription() const { return m_description; };

	int32_t getNumDgramsRead() const { return m_readBitsOn; }
	int32_t getNumDgramsSent() const { return m_sentBitsOn; }
	int32_t getNumAcksRead() const { return m_readAckBitsOn; }
	int32_t getNumAcksSent() const { return m_sentAckBitsOn; }

	msg_type_t getMsgType() const { return m_msgType; }

	int32_t getTransId() const { return m_transId; }

	uint32_t getIp() const { return m_ip; }
	uint16_t getPort() const { return m_port; }

	int64_t getTimeout() const { return m_timeout; }

	char getResendCount() const { return m_resendCount; }

	int32_t getDatagramsToSend() const { return m_dgramsToSend; }
	int32_t getDatagramsToRead() const { return m_dgramsToRead; }

	int64_t getStartTime() const { return m_startTime; }
	int64_t getLastReadTime() const { return m_lastReadTime; }
	int64_t getLastSendTime() const { return m_lastSendTime; }

	bool hasCallback() const { return (m_callback); }
	bool hasCalledHandler() const { return m_calledHandler; }
	bool hasCalledCallback() const { return m_calledCallback; }

};

#endif // GB_UDPSTATISTICS_H
