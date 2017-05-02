#include "InstanceInfoExchange.h"
#include "Hostdb.h"
#include "GbMutex.h"
#include "ScopedLock.h"
#include "Log.h"
#include "GbUtil.h"
#include "Hostdb.h"
#include "Conf.h"
#include "DailyMerge.h"
#include "Collectiondb.h"
#include "Version.h"
#include "repair_mode.h"
#include "PingServer.h"
#include "Process.h"
#include "IOBuffer.h"
#include <pthread.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <map>
#include <string>
#include <pwd.h>


//We use Vagus for the information exchange.
//Previously, PingInfo and PingServer was used but that scaled poorly to
//hundreths of instances because it would take quite a while for the
//information to be fully propagated

//The vagus cluster name is configurable (eefaulting to gb-$USER)
//The 'extra information' peice is used for piggubacking:
//  gbVersionStr
//  hosts.conf CRC
//  host flags
//  dailyMergeCollnum
//  repairMode
//  totalDocsIndexed


//We use two sockets toward the local Vagus instance. One for sending
//keepalives and one for regularly polling for instance information. It is
//tempting to use the combined "keepalivepoll" feature in vagus but if the
//thread isn't calling weAreAlive() often enough then the ( other) instance
//information will be out-of-date.

static int fd_keepalive = -1;
static int fd_pipe[2] = {-1,-1};
static bool please_shut_down=true;
static pthread_t tid;

static char vagus_cluster_name[128] = "gb";


//Connect to Vagus (blocking)
static int connect_to_vagus(int port) {
	int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(fd<0) {
		log(LOG_ERROR,"vagus: socket() failed with errno=%d  (%s)", errno, strerror(errno));
		return -1;
	}
	
	sockaddr_in sin;
	memset(&sin,0,sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	if(connect(fd,(sockaddr*)(void*)&sin,sizeof(sin))!=0) {
		log(LOG_ERROR,"vagus: connect() failed with errno=%d  (%s)", errno, strerror(errno));
		close(fd);
		return -1;
	}
	
	log(LOG_DEBUG,"vagus: Connected to Vagus on fd %d", fd);
	return fd;
}



static void process_alive_hosts(std::map<int,std::string> &alive_hosts) {
	//log(LOG_DEBUG,"vagus: got %zu alive hosts form vagus. hosts.conf says there should be %d",
	//    alive_hosts.size(), g_hostdb.getNumHosts());
	for(auto iter : alive_hosts) {
		int hostid = iter.first;
		if(hostid<0 || hostid>=g_hostdb.getNumHosts())
			continue;
		char extra_information[256];
		if(iter.second.length()>=sizeof(extra_information))
			continue;
		strcpy(extra_information,iter.second.c_str());
		char *ss=NULL;
		const char *gb_version_str = strtok_r(extra_information,";",&ss);
		const char *hosts_conf_crc_str = strtok_r(NULL,";",&ss);
		const char *host_flags_str = strtok_r(NULL,";",&ss);
		const char *daily_merge_collection_number_str = strtok_r(NULL,";",&ss);
		const char *repair_mode_str = strtok_r(NULL,";",&ss);
		const char *total_docs_indexed_str = strtok_r(NULL,";",&ss);
		if(!gb_version_str || gb_version_str[0]=='\0')
			continue;
		char *endptr;
		int hosts_conf_crc = (int)strtol(hosts_conf_crc_str,&endptr,0);
		if(endptr && *endptr)
			continue;
		int host_flags = (int)strtol(host_flags_str,&endptr,0);
		if(endptr && *endptr)
			continue;
		int daily_merge_collection_number = (int)strtol(daily_merge_collection_number_str,&endptr,0);
		if(endptr && *endptr)
			continue;
		int repair_mode = (int)strtol(repair_mode_str,&endptr,0);
		if(endptr && *endptr)
			continue;
		int total_docs_indexed = (int)strtol(total_docs_indexed_str,&endptr,0);
		if(endptr && *endptr)
			continue;
		
		//phase 1: update host fields that seem safe
		//when we get rid of PingServer entirely then this will take over

		Host *h = g_hostdb.getHost(hostid);
		
		strncpy(h->m_pingInfo.m_gbVersionStr, gb_version_str, sizeof(h->m_pingInfo.m_gbVersionStr));
		h->m_pingInfo.m_hostsConfCRC = hosts_conf_crc;
		//h->m_pingInfo.m_flags = host_flags;
		//h->m_pingInfo.m_dailyMergeCollnum = daily_merge_collection_number;
		//h->m_pingInfo.m_repairMode = repair_mode;
		h->m_pingInfo.m_totalDocsIndexed = total_docs_indexed;
	}
}


static bool do_vagus_poll(int fd) {
	char command[256];
	sprintf(command,"poll %s\n", vagus_cluster_name);
	size_t bytes_to_write = strlen(command);
	ssize_t bytes_written = write(fd,command,bytes_to_write);
	if((size_t)bytes_written!=bytes_to_write) {
		log(LOG_ERROR,"vagus: write(fd=%d) returned %zd, expected %zu, errno=%d", fd, bytes_written,bytes_to_write,errno);
		return false;
	}
	
	IOBuffer in_buf;
	for(;;) {
		in_buf.reserve_extra(65536);
		ssize_t bytes_read = read(fd, in_buf.end(), in_buf.spare());
		if(bytes_read<0) {
			log(LOG_ERROR,"vagus: read(fd=%d) from vagus failed with errno=%d (%s)",fd,errno,strerror(errno));
			return false;
		}
		if(bytes_read==0) {
			log(LOG_ERROR,"vagus: read(fd=%d) from vagus returned 0",fd);
			return false;
		}
		in_buf.push_back((size_t)bytes_read);
		
		if(in_buf.used()>=2 && in_buf.end()[-2]=='\n' && in_buf.end()[-1]=='\n')
			break;
		if(in_buf.used()==1 && in_buf.end()[-1]=='\n')
			break;
	}
	
	//nul-terminate
	in_buf.reserve_extra(1);
	*(in_buf.end()) = '\0';
	
	std::map<int,std::string> alive_hosts;
	for(char *p = in_buf.begin(); p!=in_buf.end(); ) {
		char *nl = strchr(p,'\n');
		if(!nl) break;
		*nl='\0';
		char *ss=NULL;
		char *hostid_str = strtok_r(p,":",&ss);
		char *extra_information = strtok_r(NULL,"",&ss);
		if(!hostid_str || !extra_information)
			continue;
		
		char *endptr = NULL;
		int hostid = (int)strtol(hostid_str,&endptr,0);
		if((endptr==0 || *endptr=='\0') && hostid>=0 && extra_information) {
			alive_hosts[hostid] = extra_information;
		}
		
		p = nl+1;
	}
	
	process_alive_hosts(alive_hosts);
	
	return true;
}


static void *poll_thread(void *) {
	struct pollfd pfd[2];
	memset(pfd,0,sizeof(pfd));
	pfd[0].fd = fd_pipe[0];
	pfd[0].events = POLLIN;
	pfd[1].fd = connect_to_vagus(g_conf.m_vagusPort);
	pfd[0].events = POLLIN;
	
	uint64_t next_poll_ms = 0; //poll toward vagus - not poll() syscall
	
	while(!please_shut_down) {
		uint64_t now_ms = getCurrentTimeNanoseconds()/1000000;
		if(next_poll_ms > now_ms)
			(void)poll(pfd,2,next_poll_ms-now_ms);
		
		if(please_shut_down)
			break;
		if(pfd[1].revents&(POLLIN|POLLHUP)) {
			//unexpected input or lost connection.
			(void)::close(pfd[1].fd);
			pfd[1].fd = -1;
			log(LOG_DEBUG,"vagus: lost connection to Vagus");
		}
		
		if(pfd[1].fd<0)
			pfd[1].fd = connect_to_vagus(g_conf.m_vagusPort);
		
		next_poll_ms = getCurrentTimeNanoseconds()/1000000 + g_conf.m_vagusKeepaliveSendInterval;
		if(pfd[1].fd>=0) {
			if(!do_vagus_poll(pfd[1].fd)) {
				(void)close(pfd[1].fd);
				pfd[1].fd = -1;
			}
		}
	}
	if(pfd[1].fd>=0)
		(void)::close(pfd[1].fd);
	return 0;
}


bool InstanceInfoExchange::initialize() {
	please_shut_down = false;
	
	//set vagus_cluster_name
	if(g_conf.m_vagusClusterId[0])
		strcpy(vagus_cluster_name, g_conf.m_vagusClusterId);
	else {
		//form one based on the username
		char buf[256];
		struct passwd pwd, *pwdptr;
		if(getpwuid_r(geteuid(), &pwd,buf,sizeof(buf),&pwdptr)==0) {
			sprintf(vagus_cluster_name, "gb-%s", pwd.pw_name);
			log(LOG_DEBUG,"Using vagus cluster id '%s'",vagus_cluster_name);
		} else {
			log(LOG_ERROR,"getpwuid(geteuid()...) failed with errno=%d (%s)", errno,strerror(errno));
			return false;
		}
	}
	
	if(pipe(fd_pipe)!=0) {
		log(LOG_ERROR,"pipe() failed with errno=%d (%s)", errno,strerror(errno));
		return false;
	}
	
	int rc = pthread_create(&tid,NULL,poll_thread,NULL);
	if(rc!=0) {
		log(LOG_ERROR,"pthread_create() failed with rc=%d (%s)",rc,strerror(rc));
		return false;
	}
	
	fd_keepalive = connect_to_vagus(g_conf.m_vagusPort);
	
	return true;
}


void InstanceInfoExchange::finalize() {
	please_shut_down = true;
	char dummy='d';
	(void)write(fd_pipe[1],&dummy,1);
	pthread_join(tid,NULL);
	close(fd_pipe[0]); fd_pipe[0]=-1;
	close(fd_pipe[1]); fd_pipe[1]=-1;
	close(fd_keepalive); fd_keepalive=-1;
}


void InstanceInfoExchange::weAreAlive() {
	if(fd_keepalive<0)
		fd_keepalive = connect_to_vagus(g_conf.m_vagusPort);
	if(fd_keepalive<0)
		return;

	collnum_t daily_merge_collection_number = -1;
	if(g_dailyMerge.m_cr)
		daily_merge_collection_number = g_dailyMerge.m_cr->m_collnum;

	char extra_information[256];
	sprintf(extra_information, "%s;%d;%d;%d;%d;%lu",
	        getVersion(),
		g_hostdb.getCRC(),
		getOurHostFlags(),
		daily_merge_collection_number,
		g_repairMode,
		g_process.getTotalDocsIndexed());
	
	char command[256];
	sprintf(command, "keepalive %s:%d:%u:%s\n",
	        vagus_cluster_name,
		g_hostdb.getMyHostId(),
		g_conf.m_vagusKeepaliveLifetime,
		extra_information);
	
	//log(LOG_DEBUG,"vagus: command='%s'",command);
	size_t bytes_to_write = strlen(command);
	ssize_t bytes_written = ::write(fd_keepalive, command, bytes_to_write);
	if((size_t)bytes_written != bytes_to_write) {
		log(LOG_ERROR,"vagus: write error, wrote %zd bytes, expected %zu, errno=%d", bytes_written, bytes_to_write, errno);
		::close(fd_keepalive); fd_keepalive = -1;
		return;
	}
	//log(LOG_TRACE,"vagus: sent keepalive to Vagus");
	
	char ignored_response[10];
	(void)read(fd_keepalive,ignored_response,sizeof(ignored_response));
}
