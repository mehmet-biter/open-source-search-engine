//
// Copyright (C) 2018 Privacore ApS - https://www.privacore.com
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// License TL;DR: If you change this file, you must publish your changes.
//
#ifndef FX_FXCLIENT_H
#define FX_FXCLIENT_H

#include <atomic>
#include <vector>
#include <map>
#include <memory>
#include "GbMutex.h"
#include "IOBuffer.h"


struct FxClientRequest {
	FxClientRequest(void *context, int timeout_ms);
	virtual ~FxClientRequest() = default;

	void *m_context;
	timespec m_deadline;
};

typedef std::shared_ptr<FxClientRequest> fxclient_request_ptr_t;

class FxClient {
protected:
	FxClient();
	virtual ~FxClient() = default;

	bool initialize(const char *servicename, const char *threadname, const char *hostname, int port, unsigned max_outstanding, bool log_trace);
	void reinitializeSettings(const char *hostname, int port, unsigned max_outstanding, bool log_trace);
	bool sendRequest(fxclient_request_ptr_t request);
	void finalize();

	virtual void convertRequestToWireFormat(IOBuffer *out_buffer, uint32_t seq, fxclient_request_ptr_t base_request) = 0;
	virtual void processResponse(fxclient_request_ptr_t base_request, char *response) = 0;
	virtual void errorCallback(fxclient_request_ptr_t base_request) = 0;

	bool communicationWorks() const {
		return m_communication_works;
	}

private:
	static void* communicationThread(void *);
	static int calculateEarliestTimeout(const std::map<uint32_t, fxclient_request_ptr_t> &outstanding_requests);

	void drainWakeupPipe();
	void waitForTimeToConnect();
	int runConnectLoop();
	void runCommunicationLoop(int fd);
	void processInBuffer(IOBuffer *in_buffer, std::map<uint32_t, fxclient_request_ptr_t> *outstanding_requests);
	void finishQueuedRequests();

	std::vector<fxclient_request_ptr_t> m_queued_requests;
	GbMutex m_queued_requests_mtx;
	pthread_t m_tid;
	bool m_stop;
	int m_wakeup_fd[2];
	time_t m_next_connect_attempt;
	std::atomic<bool> m_communication_works;
	std::atomic<unsigned> m_outstanding_request_count;

	const char *m_servicename;
	const char *m_hostname;
	int m_port;
	unsigned m_max_outstanding;
	bool m_log_trace;
};


#endif //FX_FXCLIENT_H
