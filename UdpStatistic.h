#ifndef GB_UDPSTATISTIC_H
#define GB_UDPSTATISTIC_H

#include <stdint.h>
#include <vector>
#include "MsgType.h"

class UdpSlot;

class UdpStatistic {
public:
	UdpStatistic(const UdpSlot &slot);

	int32_t getTransId() const { return m_transId; }
	uint32_t getIp() const { return m_ip; }
	uint16_t getPort() const { return m_port; }

	msg_type_t getMsgType() const { return m_msgType; }
	const char* getDescription() const { return m_description; };

	int32_t getNiceness() const { return m_niceness; }
	char getConvertedNiceness() const { return m_convertedNiceness; }

	int32_t getNumDatagramRead() const { return m_numDatagramRead; }
	int32_t getNumDatagramSent() const { return m_numDatagramSent; }
	int32_t getNumAckRead() const { return m_numAckRead; }
	int32_t getNumAckSent() const { return m_numAckSent; }
	int32_t getNumPendingRead() const { return m_numPendingRead; }
	int32_t getNumPendingSend() const { return m_numPendingSend; }

	char getResendCount() const { return m_resendCount; }

	int64_t getTimeout() const { return m_timeout; }
	int64_t getStartTime() const { return m_startTime; }
	int64_t getLastReadTime() const { return m_lastReadTime; }
	int64_t getLastSendTime() const { return m_lastSendTime; }

	bool hasCallback() const { return m_hasCallback; }
	bool hasCalledHandler() const { return m_hasCalledHandler; }
	bool hasCalledCallback() const { return m_hasCalledCallback; }

	const char* getExtraInfo() const { return m_extraInfo; }

private:
	int32_t m_transId;
	uint32_t m_ip;
	uint16_t m_port;

	msg_type_t m_msgType;
	char m_description[255];

	int32_t m_niceness;
	char m_convertedNiceness;

	int32_t m_numDatagramRead;
	int32_t m_numDatagramSent;
	int32_t m_numAckRead;
	int32_t m_numAckSent;
	int32_t m_numPendingRead;
	int32_t m_numPendingSend;

	char m_resendCount;

	int64_t m_timeout;
	int64_t m_startTime;
	int64_t m_lastReadTime;
	int64_t m_lastSendTime;

	bool m_hasCallback;
	bool m_hasCalledHandler;
	bool m_hasCalledCallback;

	char m_extraInfo[64];
};

#endif // GB_UDPSTATISTIC_H
