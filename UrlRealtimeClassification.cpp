#include "UrlRealtimeClassification.h"
#include "Conf.h"
#include "GbMutex.h"
#include "ScopedLock.h"
#include "Log.h"
#include "IOBuffer.h"
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <pthread.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string>
#include <vector>
#include <map>
#include <time.h>
#include <errno.h>
#include <atomic>


// The protocol is very simple.
// The server Receives queries in the form
//	<query-id>:<url><NL>
// (The client must escape-encode the URL)
//
// Supports both pipelining and streaming
//
// The server responses:
//	<query-id>:[<flag>['+'<flag>...]]


// Structure of the implementation:
//   Callers queue requests in 'queued_requests' and wake up the communication thread with a pipe write.
//   The communication thread runs in a poll/connect/read/write loop, sendign requests and receiving responses.
//   The communication thread calls the callbacks. todo: call the callbacks in a separate thread.



namespace {
struct Request {
	url_realtime_classification_callback_t callback;
	void *context;
	std::string url;
	timespec deadline;
	Request() : callback(0), context(0), url("") {}
	Request(url_realtime_classification_callback_t callback_,
			  void *context_,
			  const std::string &url_,
			  const timespec &deadline_)
	  : callback(callback_),
	    context(context_),
	    url(url_),
	    deadline(deadline_)
	{}
};
} //anonymous namespace

static std::vector<Request> queued_requests;
static GbMutex mtx_queued_requests;
static pthread_t tid;
static bool please_stop = false;
static int wakeup_fd[2];
static time_t next_connect_attempt = 0;
static std::atomic<bool> communication_works(false);
static std::atomic<unsigned> outstanding_request_count(0);


static void drainWakeupPipe() {
	char buf[1024];
	while(read(wakeup_fd[0],buf,sizeof(buf))>0)
		;
}


//Wait until next_connect_attempt or until someone wakes us
static void waitForTimeToConnect() {
	time_t now = time(0);
	if(now>=next_connect_attempt)
		return;
	struct pollfd pfd;
	memset(&pfd,0,sizeof(pfd));
	pfd.fd = wakeup_fd[0];
	(void)poll(&pfd,1,(next_connect_attempt-now)*1000);
	drainWakeupPipe();
}


static int runConnectLoop(const char *hostname, int port_number) {
	next_connect_attempt = time(0) + 30;
	
	if(!hostname)
		return -1;
	if(!hostname[0])
		return -1;
	if(port_number<=0)
		return -1;
	char port_number_str[16];
	sprintf(port_number_str,"%d",port_number);
	
	addrinfo hints;
	memset(&hints,0,sizeof(hints));
	hints.ai_flags = AI_ADDRCONFIG;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	
	addrinfo *res;
	int rc = getaddrinfo(hostname, port_number_str, &hints, &res);
	if(rc!=0) {
		log(LOG_ERROR,"url-classification: getaddrinfo(%s...) failed with rc=%d (%s)",hostname,rc,gai_strerror(rc));
		return -1;
	}
	
	int most_recent_error=0;
	for(addrinfo *ai = res; ai; ai=ai->ai_next) {
		int fd = socket(ai->ai_family,ai->ai_socktype,ai->ai_protocol);
		if(fd<0) {
			most_recent_error=errno;
			continue;
		}
		fcntl(fd,F_SETFL,O_NONBLOCK);
		
		if(connect(fd,ai->ai_addr,ai->ai_addrlen)==0) {
			//Immediate connect. Usually only happenes on solaris when connecting over the
			//loopback interface. But maybe someday linux will do the same.
			freeaddrinfo(res);
			return fd;
		}
		if(errno!=EINPROGRESS) {
			most_recent_error = errno;
			close(fd);
			continue;
		}
		
		struct pollfd pfd[2];
		memset(pfd,0,sizeof(pfd));
		pfd[0].fd = fd;
		pfd[0].events = POLLOUT;
		pfd[1].fd = wakeup_fd[0];
		pfd[1].events = POLLIN;
		
		(void)poll(pfd,2,-1);
		
		if(pfd[0].revents&(POLLOUT|POLLHUP|POLLERR)) {
			//connection status available
			int err;
			socklen_t size = (socklen_t)sizeof(err);
			if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&err,&size)!=-1 && err) {
				//connection failed
				most_recent_error = err;
				close(fd);
				continue;
			}
			//connected
			freeaddrinfo(res);
			log(LOG_INFO,"url-classification: connected to %s:%d",hostname,port_number);
			return fd;
		}
		if(please_stop) {
			freeaddrinfo(res);
			return -1;
		}
        }
	freeaddrinfo(res);
	//we ran through all the resolved addresses and failed to connect to any of them
	log(LOG_ERROR,"Could not connect to url classification services, most recent errno=%d (%s)",most_recent_error,strerror(most_recent_error));
	
	return -1;
}


static void convertRequestToWireFormat(IOBuffer *out_buffer, uint32_t seq, const Request &r) {
	out_buffer->reserve_extra(8+1+r.url.size()+1);
	sprintf(out_buffer->end(),"%08x",seq);
	out_buffer->push_back(8);
	out_buffer->end()[0] = ':';
	out_buffer->push_back(1);
	memcpy(out_buffer->end(),r.url.data(),r.url.size());
	out_buffer->push_back(r.url.size());
	out_buffer->end()[0] = '\n';
	out_buffer->push_back(1);
}


static void processInBuffer(IOBuffer *in_buffer, std::map<uint32_t,Request> *outstanding_requests) {
	if(g_conf.m_logTraceUrlClassification)
		log(LOG_TRACE,"url-classification:  in_buffer: %*.*s", (int)in_buffer->used(), (int)in_buffer->used(), in_buffer->begin());
	while(!in_buffer->empty()) {
		char *nl = (char*)memchr(in_buffer->begin(),'\n',in_buffer->used());
		if(!nl)
			break;
		char *response_start = in_buffer->begin();
		//parse the seq:[flag[+flag...]]
		*nl ='\0';
		char *colon = strchr(response_start,':');
		if(colon) {
			*colon = '\0';
			uint32_t seq;
			if(sscanf(response_start,"%" SCNx32, &seq)==1) {
				char *ss = NULL;
				uint32_t classification = 0;
				for(const char *s=strtok_r(colon+1,"+",&ss); s; s=strtok_r(NULL,"+",&ss)) {
					if(strcmp(s,"MALWARE")==0)
						classification |= URL_CLASSIFICATION_MALICIOUS;
					//todo: else ... other classifications
				}
				auto iter = outstanding_requests->find(seq);
				if(iter!=outstanding_requests->end()) {
					if(g_conf.m_logTraceUrlClassification)
						log(LOG_TRACE,"url-classification: Got classification %08x or %s",classification,iter->second.url.c_str());
					(*iter->second.callback)(iter->second.context,classification);
					outstanding_requests->erase(iter);
					outstanding_request_count--;
				} else
					log(LOG_WARN,"url-classification: Got unmatched response for seq %08x",seq);
			}
		} else
			log(LOG_WARN,"url-classification: received incorrectly formatted response: %s",response_start);
		
		in_buffer->pop_front(nl+1-response_start);
	}
}


static int calculateEarliestTimeout(const std::map<uint32_t,Request> &outstanding_requests) {
	if(outstanding_requests.empty())
		return -1;
	auto iter = outstanding_requests.begin();
	timespec earliest_deadline = iter->second.deadline;
	++iter;
	for(; iter!=outstanding_requests.end(); ++iter) {
		if(iter->second.deadline.tv_sec>earliest_deadline.tv_sec)
			earliest_deadline = iter->second.deadline;
		else if(iter->second.deadline.tv_sec==earliest_deadline.tv_sec &&
		        iter->second.deadline.tv_nsec>earliest_deadline.tv_nsec)
			earliest_deadline = iter->second.deadline;
	}
	timespec now;
	clock_gettime(CLOCK_REALTIME,&now);
	int timeout = (now.tv_sec - earliest_deadline.tv_sec) * 1000 +
	              (now.tv_nsec - earliest_deadline.tv_nsec) / 1000000;
	if(timeout<0)
		return 0;  //already passed
	return timeout;
}


static void runCommunicationLoop(int fd) {
	communication_works = true;
	IOBuffer in_buffer;
	IOBuffer out_buffer;
	std::map<uint32_t,Request> outstanding_requests;
	uint32_t request_sequencer = 0;
	
	while(!please_stop) {
		int timeout = calculateEarliestTimeout(outstanding_requests);
		
		struct pollfd pfd[2];
		memset(pfd,0,sizeof(pfd));
		pfd[0].fd = fd;
		pfd[0].events = POLLIN;
		if(!out_buffer.empty())
			pfd[0].events |= POLLOUT;
		pfd[1].fd = wakeup_fd[0];
		pfd[1].events = POLLIN;
		
		int rc = poll(pfd,2,timeout);
		
		if(rc<0) {
			log(LOG_ERROR,"url-classification: poll() failed with errno=%d (%s)",errno,strerror(errno));
			continue;
		}
		if(pfd[0].revents & POLLIN) {
			in_buffer.reserve_extra(8192);
			ssize_t bytes_read = ::read(fd,in_buffer.end(),in_buffer.spare());
			if(bytes_read<0) {
				log(LOG_ERROR,"url-classification: read(%d) failed with errno=%d (%s)",fd,errno,strerror(errno));
				break;
			}
			if(bytes_read==0) {
				log(LOG_INFO,"url-classification: read(%d) returned 0 (server closed socket)", fd);
				break;
			}
			if(g_conf.m_logTraceUrlClassification)
				log(LOG_TRACE,"url-classification: Received %zd bytes from classification server",bytes_read);
			in_buffer.push_back((size_t)bytes_read);
			processInBuffer(&in_buffer,&outstanding_requests);
		}
		if(pfd[0].revents&POLLOUT && !out_buffer.empty()) {
			ssize_t bytes_written = ::write(fd,out_buffer.begin(),out_buffer.used());
			if(bytes_written<0) {
				log(LOG_ERROR,"url-classification: write(%d) failed with errno=%d (%s)",fd,errno,strerror(errno));
				break;
			}
			if(g_conf.m_logTraceUrlClassification)
				log(LOG_TRACE,"Sent %zu bytes to classification server",bytes_written);
			out_buffer.pop_front((size_t)bytes_written);
		}
		if(pfd[1].revents&POLLIN) {
			drainWakeupPipe();
			std::vector<Request> tmp;
			{
				ScopedLock sl(mtx_queued_requests);
				tmp.swap(queued_requests);
			}
			for(auto const &r : tmp) {
				uint32_t seq = request_sequencer++;
				convertRequestToWireFormat(&out_buffer,seq,r);
				outstanding_requests[seq] = r;
				outstanding_request_count++;
			}
		}
		//timeout out requests
		timespec now;
		clock_gettime(CLOCK_REALTIME,&now);
		for(auto iter=outstanding_requests.begin(); iter!=outstanding_requests.end(); ) {
			if(iter->second.deadline.tv_sec>now.tv_sec ||
			   (iter->second.deadline.tv_sec==now.tv_sec && iter->second.deadline.tv_nsec>now.tv_nsec))
				++iter;
			else {
				(iter->second.callback)(iter->second.context,URL_CLASSIFICATION_UNKNOWN);
				iter = outstanding_requests.erase(iter);
			}
			   
		}
	}
	
	communication_works = false;
	drainWakeupPipe();
	//call callbacks on queued and oustanding requests
	for(auto const &r : outstanding_requests)
		(*r.second.callback)(r.second.context,URL_CLASSIFICATION_UNKNOWN);
	outstanding_request_count = 0;
}


static void finishQueuedRequests() {
	std::vector<Request> tmp;
	{
		ScopedLock sl(mtx_queued_requests);
		tmp.swap(queued_requests);
	}
	for(auto const &r : tmp)
		(*r.callback)(r.context,URL_CLASSIFICATION_UNKNOWN);
}


static void *communicationThread(void *) {
	while(!please_stop) {
		waitForTimeToConnect();
		if(please_stop)
			break;
		int fd = runConnectLoop(g_conf.m_urlClassificationServerName,g_conf.m_urlClassificationServerPort);
		if(please_stop)
			break;
		if(fd>=0) {
			runCommunicationLoop(fd);
			(void)close(fd);
		}
		finishQueuedRequests();
	}
	return NULL;
}


bool initializeRealtimeUrlClassification() {
	please_stop = false;
	communication_works = false;
	
	if(pipe(wakeup_fd)!=0) {
		log(LOG_ERROR,"pipe() failed with errno=%d (%s)", errno, strerror(errno));
		return false;
	}
	fcntl(wakeup_fd[0],F_SETFL,O_NONBLOCK);
	fcntl(wakeup_fd[1],F_SETFL,O_NONBLOCK);
	
	int rc = pthread_create(&tid,NULL,communicationThread,NULL);
	if(rc!=0) {
		close(wakeup_fd[0]);
		close(wakeup_fd[1]);
		log(LOG_ERROR,"pthread_create() failed with rc=%d (%s)",rc,strerror(rc));
		return false;
	}
	
	//set thread name so "perf top" et al can show a nice name
	pthread_setname_np(tid,"urlclass");
	
	return true;
}


bool classifyUrl(const char *url, url_realtime_classification_callback_t callback, void *context) {
	timespec deadline;
	clock_gettime(CLOCK_REALTIME,&deadline);
	deadline.tv_sec += g_conf.m_urlClassificationTimeout/1000;
	deadline.tv_nsec += (g_conf.m_urlClassificationTimeout%1000)*1000000;
	if(deadline.tv_nsec>=1000000000) {
		deadline.tv_sec++;
		deadline.tv_nsec -= 1000000000;
	}
	if(outstanding_request_count >= g_conf.m_maxOutstandingUrlClassifications)
		return false;
	ScopedLock sl(mtx_queued_requests);
	if(!communication_works)
		return false;
	if(outstanding_request_count + queued_requests.size() >= g_conf.m_maxOutstandingUrlClassifications)
		return false;
	queued_requests.emplace_back(callback,context,url,deadline);
	char dummy='d';
	(void)write(wakeup_fd[1],&dummy,1);
	return true;
}


void finalizeRealtimeUrlClassification() {
	please_stop = true;
	char dummy='d';
	(void)write(wakeup_fd[1],&dummy,1);
	
	(void)pthread_join(tid,NULL);
	
	close(wakeup_fd[0]);
	wakeup_fd[0]=-1;
	close(wakeup_fd[1]);
	wakeup_fd[1]=-1;
	communication_works = false;
	next_connect_attempt = 0;
	//todo: should actually do assert(queued_requests.empty());
}
