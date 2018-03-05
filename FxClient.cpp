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
#include "FxClient.h"
#include "Conf.h"
#include "ScopedLock.h"
#include "Log.h"
#include "UrlRealtimeClassification.h"
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <pthread.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <time.h>
#include <errno.h>


// The protocol is very simple.
// The server Receives queries in the form
//	<query-id>:<data><NL>
//
// Supports both pipelining and streaming
//
// The server responses:
//	<query-id>:<data><NL>


// Structure of the implementation:
//   Callers queue requests in 'm_queued_requests' and wake up the communication thread with a pipe write.
//   The communication thread runs in a poll/connect/read/write loop, sendign requests and receiving responses.
//   The communication thread calls the callbacks. todo: call the callbacks in a separate thread.

FxClientRequest::FxClientRequest(void *context, int timeout_ms)
	: m_context(context)
	, m_deadline() {
	clock_gettime(CLOCK_REALTIME, &m_deadline);
	m_deadline.tv_sec += timeout_ms / 1000;
	m_deadline.tv_nsec += (timeout_ms % 1000) * 1000000;
	if (m_deadline.tv_nsec >= 1000000000) {
		m_deadline.tv_sec++;
		m_deadline.tv_nsec -= 1000000000;
	}
}

FxClient::FxClient()
	: m_queued_requests()
	, m_queued_requests_mtx()
	, m_tid(0)
	, m_stop(false)
	, m_wakeup_fd { -1, -1 }
	, m_next_connect_attempt(0)
	, m_communication_works(false)
	, m_outstanding_request_count(0)
	, m_servicename(nullptr)
	, m_hostname(nullptr)
	, m_port(0) {
}

void FxClient::drainWakeupPipe() {
	char buf[1024];
	while (read(m_wakeup_fd[0], buf, sizeof(buf)) > 0);
}

//Wait until m_next_connect_attempt or until someone wakes us
void FxClient::waitForTimeToConnect() {
	time_t now = time(0);
	if (now >= m_next_connect_attempt)
		return;
	struct pollfd pfd;
	memset(&pfd, 0, sizeof(pfd));
	pfd.fd = m_wakeup_fd[0];
	pfd.events = POLLIN;
	(void)poll(&pfd, 1, (m_next_connect_attempt - now) * 1000);
	drainWakeupPipe();
}

int FxClient::runConnectLoop() {
	m_next_connect_attempt = time(nullptr) + 30;

	if (!m_hostname)
		return -1;
	if (!m_hostname[0])
		return -1;
	if (m_port <= 0)
		return -1;
	char port_number_str[16];
	sprintf(port_number_str, "%d", m_port);

	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_ADDRCONFIG;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	addrinfo *res;
	int rc = getaddrinfo(m_hostname, port_number_str, &hints, &res);
	if (rc != 0) {
		log(LOG_ERROR, "client(%s): getaddrinfo(%s...) failed with rc=%d (%s)", m_servicename, m_hostname, rc, gai_strerror(rc));
		return -1;
	}

	int most_recent_error = 0;
	for (addrinfo *ai = res; ai; ai = ai->ai_next) {
		int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0) {
			most_recent_error = errno;
			continue;
		}
		if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
			most_recent_error = errno;
			close(fd);
			continue;
		}

		if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
			//Immediate connect. Usually only happenes on solaris when connecting over the
			//loopback interface. But maybe someday linux will do the same.
			freeaddrinfo(res);
			return fd;
		}
		if (errno != EINPROGRESS) {
			most_recent_error = errno;
			close(fd);
			continue;
		}

		struct pollfd pfd[2];
		memset(pfd, 0, sizeof(pfd));
		pfd[0].fd = fd;
		pfd[0].events = POLLOUT;
		pfd[1].fd = m_wakeup_fd[0];
		pfd[1].events = POLLIN;

		(void)poll(pfd, 2, -1);

		if (pfd[0].revents & (POLLOUT | POLLHUP | POLLERR)) {
			//connection status available
			int err;
			socklen_t size = (socklen_t)sizeof(err);
			if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &size) != -1 && err) {
				//connection failed
				most_recent_error = err;
				close(fd);
				continue;
			}
			//connected
			freeaddrinfo(res);
			log(LOG_INFO, "client(%s): connected to %s:%d", m_servicename, m_hostname, m_port);
			return fd;
		}
		if (m_stop) {
			freeaddrinfo(res);
			return -1;
		}
	}
	freeaddrinfo(res);
	//we ran through all the resolved addresses and failed to connect to any of them
	log(LOG_ERROR, "client(%s): Could not connect to server(%s:%d), most recent errno=%d (%s)",
	    m_servicename, m_hostname, m_port, most_recent_error, strerror(most_recent_error));

	return -1;
}

int FxClient::calculateEarliestTimeout(const std::map<uint32_t, fxclient_request_ptr_t> &outstanding_requests) {
	if (outstanding_requests.empty()) {
		return -1;
	}

	auto iter = outstanding_requests.begin();
	timespec earliest_deadline = iter->second->m_deadline;
	++iter;

	for (; iter != outstanding_requests.end(); ++iter) {
		if (iter->second->m_deadline.tv_sec > earliest_deadline.tv_sec) {
			earliest_deadline = iter->second->m_deadline;
		} else if (iter->second->m_deadline.tv_sec == earliest_deadline.tv_sec &&
		           iter->second->m_deadline.tv_nsec > earliest_deadline.tv_nsec) {
			earliest_deadline = iter->second->m_deadline;
		}
	}

	timespec now;
	clock_gettime(CLOCK_REALTIME, &now);
	int timeout = (now.tv_sec - earliest_deadline.tv_sec) * 1000 +
	              (now.tv_nsec - earliest_deadline.tv_nsec) / 1000000;
	if (timeout < 0)
		return 0;  //already passed

	return timeout;
}

void FxClient::processInBuffer(IOBuffer *in_buffer, std::map<uint32_t, fxclient_request_ptr_t> *outstanding_requests) {
	logTrace(m_log_trace, "client(%s): in_buffer: %*.*s", m_servicename, (int)in_buffer->used(), (int)in_buffer->used(), in_buffer->begin());

	while (!in_buffer->empty()) {
		char *nl = (char *)memchr(in_buffer->begin(), '\n', in_buffer->used());
		if (!nl) {
			break;
		}

		char *response_start = in_buffer->begin();
		*nl = '\0';

		//parse the seq
		char *colon = strchr(response_start, ':');
		if (colon) {
			*colon = '\0';
			uint32_t seq;
			if (sscanf(response_start, "%" SCNx32, &seq) == 1) {
				auto iter = outstanding_requests->find(seq);
				if (iter != outstanding_requests->end()) {
					processResponse(iter->second, colon + 1);
					outstanding_requests->erase(iter);
					--m_outstanding_request_count;
				} else {
					log(LOG_WARN, "client(%s): Got unmatched response for seq %08x", m_servicename, seq);
				}
			}
		} else {
			log(LOG_WARN, "client(%s): received incorrectly formatted response: %s", m_servicename, response_start);
		}

		in_buffer->pop_front(nl + 1 - response_start);
	}
}

void FxClient::runCommunicationLoop(int fd) {
	m_communication_works = true;
	IOBuffer in_buffer;
	IOBuffer out_buffer;
	std::map<uint32_t, fxclient_request_ptr_t> outstanding_requests;
	uint32_t request_sequencer = 0;

	while (!m_stop) {
		int timeout = calculateEarliestTimeout(outstanding_requests);

		struct pollfd pfd[2];
		memset(pfd, 0, sizeof(pfd));
		pfd[0].fd = fd;
		pfd[0].events = POLLIN;
		if (!out_buffer.empty()) {
			pfd[0].events |= POLLOUT;
		}

		pfd[1].fd = m_wakeup_fd[0];
		pfd[1].events = POLLIN;

		int rc = poll(pfd, 2, timeout);
		if (rc < 0) {
			log(LOG_ERROR, "client(%s): poll() failed with errno=%d (%s)", m_servicename, errno, strerror(errno));
			continue;
		}

		if (pfd[0].revents & POLLIN) {
			in_buffer.reserve_extra(8192);
			ssize_t bytes_read = ::read(fd, in_buffer.end(), in_buffer.spare());
			if (bytes_read < 0) {
				log(LOG_ERROR, "client(%s): read(%d) failed with errno=%d (%s)", m_servicename, fd, errno, strerror(errno));
				break;
			}
			if (bytes_read == 0) {
				log(LOG_INFO, "client(%s): read(%d) returned 0 (server closed socket)", m_servicename, fd);
				break;
			}
			logTrace(m_log_trace, "client(%s): Received %zd bytes from server", m_servicename, bytes_read);
			in_buffer.push_back((size_t)bytes_read);
			processInBuffer(&in_buffer, &outstanding_requests);
		}

		if (pfd[0].revents & POLLOUT && !out_buffer.empty()) {
			ssize_t bytes_written = ::write(fd, out_buffer.begin(), out_buffer.used());
			if (bytes_written < 0) {
				log(LOG_ERROR, "client(%s): write(%d) failed with errno=%d (%s)", m_servicename, fd, errno, strerror(errno));
				break;
			}
			logTrace(m_log_trace, "client(%s): Sent %zu bytes to server", m_servicename, bytes_written);
			out_buffer.pop_front((size_t)bytes_written);
		}

		if (pfd[1].revents & POLLIN) {
			drainWakeupPipe();
			std::vector<fxclient_request_ptr_t> tmp;
			{
				ScopedLock sl(m_queued_requests_mtx);
				tmp.swap(m_queued_requests);
			}

			for (auto const &r : tmp) {
				uint32_t seq = request_sequencer++;
				convertRequestToWireFormat(&out_buffer, seq, r);
				outstanding_requests[seq] = r;
				m_outstanding_request_count++;
			}
		}

		//timeout out requests
		timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		for (auto iter = outstanding_requests.begin(); iter != outstanding_requests.end();) {
			if (iter->second->m_deadline.tv_sec > now.tv_sec ||
			    (iter->second->m_deadline.tv_sec == now.tv_sec && iter->second->m_deadline.tv_nsec > now.tv_nsec)) {
				++iter;
			} else {
				errorCallback(iter->second);
				iter = outstanding_requests.erase(iter);
			}
		}
	}

	m_communication_works = false;
	drainWakeupPipe();

	//call callbacks on queued and oustanding requests
	for (auto const &r : outstanding_requests) {
		errorCallback(r.second);
	}

	m_outstanding_request_count = 0;
}

void FxClient::finishQueuedRequests() {
	std::vector<fxclient_request_ptr_t> tmp;
	{
		ScopedLock sl(m_queued_requests_mtx);
		tmp.swap(m_queued_requests);
	}

	for (auto const &r : tmp) {
		errorCallback(r);
	}
}

void *FxClient::communicationThread(void *args) {
	FxClient *client = static_cast<FxClient*>(args);

	while (!client->m_stop) {
		client->waitForTimeToConnect();
		if (client->m_stop)
			break;
		int fd = client->runConnectLoop();
		if (client->m_stop)
			break;
		if (fd >= 0) {
			client->runCommunicationLoop(fd);
			(void)close(fd);
		}
		client->finishQueuedRequests();
	}

	return nullptr;
}


bool FxClient::initialize(const char *servicename, const char *threadname, const char *hostname, int port, unsigned max_outstanding, bool log_trace) {
	reinitializeSettings(hostname, port, max_outstanding, log_trace);

	if (pipe(m_wakeup_fd) != 0) {
		log(LOG_ERROR, "client: pipe() failed with errno=%d (%s)", errno, strerror(errno));
		return false;
	}
	if (fcntl(m_wakeup_fd[0], F_SETFL, O_NONBLOCK) < 0 ||
	    fcntl(m_wakeup_fd[1], F_SETFL, O_NONBLOCK) < 0) {
		log(LOG_ERROR, "client: fcntl(pipe,nonblock) failed with errno=%d (%s)", errno, strerror(errno));
		close(m_wakeup_fd[0]);
		close(m_wakeup_fd[1]);
		return false;
	}

	m_servicename = servicename;

	int rc = pthread_create(&m_tid, nullptr, communicationThread, this);
	if (rc != 0) {
		close(m_wakeup_fd[0]);
		close(m_wakeup_fd[1]);
		log(LOG_ERROR, "client: pthread_create() failed with rc=%d (%s)", rc, strerror(rc));
		return false;
	}

	//set thread name so "perf top" et al can show a nice name
	pthread_setname_np(m_tid, threadname);

	return true;
}

void FxClient::reinitializeSettings(const char *hostname, int port, unsigned max_outstanding, bool log_trace) {
	m_hostname = hostname;
	m_port = port;
	m_max_outstanding = max_outstanding;
	m_log_trace = log_trace;
}

bool FxClient::sendRequest(fxclient_request_ptr_t request) {
	if (m_outstanding_request_count >= m_max_outstanding) {
		return false;
	}

	ScopedLock sl(m_queued_requests_mtx);
	if (!m_communication_works) {
		return false;
	}

	if (m_outstanding_request_count + m_queued_requests.size() >= m_max_outstanding) {
		return false;
	}

	m_queued_requests.emplace_back(request);

	char dummy = 'd';
	(void)write(m_wakeup_fd[1], &dummy, 1);

	return true;
}

void FxClient::finalize() {
	if (m_wakeup_fd[1] < 0) {
		return; //not active
	}

	m_stop = true;
	char dummy = 'd';
	(void)write(m_wakeup_fd[1], &dummy, 1);

	(void)pthread_join(m_tid, nullptr);

	close(m_wakeup_fd[0]);
	m_wakeup_fd[0] = -1;

	close(m_wakeup_fd[1]);
	m_wakeup_fd[1] = -1;

	m_communication_works = false;
	m_next_connect_attempt = 0;

	//todo: should actually do assert(queued_requests.empty());
}
