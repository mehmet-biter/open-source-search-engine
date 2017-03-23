#include "MergeSpaceCoordinator.h"
#include "Log.h"
#include "ScopedLock.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <utime.h>
#include <signal.h>
#include <stdlib.h>


void *hold_lock_thread_function_trampoline(void *pv);

static const time_t touch_interval = 30;


static std::string make_lock_filename(const std::string &lock_dir, int lock_number) {
	char name[16];
	sprintf(name,"lock%d",lock_number);
	return lock_dir + "/" + name;
}


MergeSpaceCoordinator::MergeSpaceCoordinator(const char *lock_dir_, int min_lock_files_, const char *merge_space_dir_)
  : hold_lock_thread_running(false),
    mtx(),
    please_shutdown(false),
    held_lock_number(-1),
    lock_dir(lock_dir_),
    min_lock_files(min_lock_files_),
    merge_space_dir(merge_space_dir_)
{
	pthread_cond_init(&cond,NULL);
	int rc = pthread_create(&hold_lock_tid,NULL,hold_lock_thread_function_trampoline,this);
	if(rc!=0) {
		log(LOG_ERROR,"pthread_create() returned %d (%s)", rc, strerror(rc));
		return;
	}
	hold_lock_thread_running = true;
}


MergeSpaceCoordinator::~MergeSpaceCoordinator() {
	relinquish();
	if(hold_lock_thread_running) {
		pthread_mutex_lock(&mtx.mtx);
		please_shutdown = true;
		pthread_cond_signal(&cond);
		pthread_mutex_unlock(&mtx.mtx);
		pthread_join(hold_lock_tid,NULL);
		hold_lock_thread_running = false;
	}
}


bool MergeSpaceCoordinator::acquire(uint64_t how_much) {
	if(please_shutdown)
		return false;
	
	if(held_lock_number>=0)
		return true;
	
	if(min_lock_files<=0) {
		log(LOG_ERROR,"MergeSpaceCoordinator::acquire: min_lock_files=%d. Locking will never succeed",min_lock_files);
		return false;
	}
	
	struct statvfs svfs;
	if(statvfs(merge_space_dir.c_str(),&svfs)==0) {
		unsigned long free_bytes = svfs.f_bavail * svfs.f_bsize;
		if(free_bytes < how_much)
			return false;
	}
	
	//verify or create lock directory
	struct stat st;
	if(stat(lock_dir.c_str(),&st)==0) {
		if(S_ISDIR(st.st_mode))
			; //excellent
		else
			log(LOG_ERROR,"Lock directory %s is not a directory", lock_dir.c_str());
	} else {
		//lock directory doesn't exist. Try to create it.
		//the equivalent of "mkdir -p" would be nice but not necessary
		(void)mkdir(lock_dir.c_str(),0777);
	}
	
	for(int lock_number=0; ; lock_number++) {
		std::string filename(make_lock_filename(lock_dir,lock_number));
		int fd;
		if(lock_number<min_lock_files)
			fd = open(filename.c_str(), O_RDWR|O_CREAT, 0666);
		else
			fd = open(filename.c_str(), O_RDWR, 0666);
		if(fd>=0) {
			char buf[16];
			ssize_t bytes_read = pread(fd,buf,sizeof(buf)-1,0);
			if(bytes_read<0) {
				log(LOG_ERROR,"read(%s) failed with errno %d (%s)", filename.c_str(), errno, strerror(errno));
				close(fd);
				continue;
			} else if(bytes_read>0) {
				buf[bytes_read] = '\0';
				char *endptr=NULL;
				long pid = strtol(buf,&endptr,10);
				if(endptr==NULL || !*endptr) {
					//perhaps it is a pid of a running process
					if(kill((pid_t)pid,0)==0) {
						//it is a pid of a running process. Has the file actually been touched the past 30 seconds?
						struct stat st2;
						if(fstat(fd,&st2)!=0) {
							log(LOG_ERROR,"fstat(%s) failed with errno %d (%s)", filename.c_str(), errno, strerror(errno));
							close(fd);
							continue;
						}
						if(st2.st_mtim.tv_sec + touch_interval*2+1 > time(0)) {
							//touched the past 61 seconds, so the process holds the lock
							close(fd);
							continue;
						}
					} else {
						//old pid in file. truncate it
						log(LOG_DEBUG,"Old pid %ld found in %s, or errno=%d", pid, filename.c_str(), errno);
						(void)ftruncate(fd,0);
					}
				} else {
					//junk in file. truncate it
					log(LOG_DEBUG,"Junk in %s", filename.c_str());
					(void)ftruncate(fd,0);
				}
			} else {
				//empty - assume it is unheld
			}
			//ok, the lock appears unheld.
			sprintf(buf,"%d",(int)getpid());
			ssize_t bytes_written = pwrite(fd,buf,strlen(buf),0);
			if(bytes_written<0) {
				log(LOG_ERROR,"pwrite(%s) failed with errno %d (%s)", filename.c_str(), errno, strerror(errno));
				close(fd);
				continue;
			} else if((size_t)bytes_written<strlen(buf)) {
				//what?
				log(LOG_ERROR,"pwrite(%s,%ld) returned %ld", filename.c_str(), strlen(buf), bytes_written);
				close(fd);
				continue;
			}
			//we wrote our pid to the file. Wait a bit and then verify that it has not been overwritten
			timespec ts;
			ts.tv_sec = 1;
			ts.tv_nsec = 0;
			nanosleep(&ts,NULL);
			bytes_read = pread(fd,buf,sizeof(buf)-1,0);
			close(fd);
			if(bytes_read!=bytes_written) {
				//file size changed or there was a read error. In either case the lock failed
				log(LOG_DEBUG,"%s changed size", filename.c_str());
				continue;
			}
			buf[bytes_read] = '\0';
			char *endptr=NULL;
			long pid = strtol(buf,&endptr,10);
			if(endptr!=NULL && *endptr) {
				log(LOG_DEBUG,"Junk found in %s", filename.c_str());
				continue;
			}
			if(pid!=getpid()) {
				log(LOG_DEBUG,"New pid (%d) found in %s (our pid = %d)", (int)pid, filename.c_str(), (int)getpid());
				continue;
			}
			

			// lock is not needed but it shuts up thread sanitizer
			ScopedLock sl(mtx);

			//hurray!
			held_lock_number = lock_number;
			log(LOG_DEBUG,"MergeSpaceCoordinator::acquire: got lock #%d for %" PRIu64" bytes", held_lock_number, how_much);
			return true;
		} else {
			if(lock_number<min_lock_files)
				log(LOG_ERROR,"open(%s|O_CREAT) failed with errno %d (%s)", filename.c_str(), errno, strerror(errno));
			else
				break;
		}
	}
	return false;
}


bool MergeSpaceCoordinator::acquired() const {
	return held_lock_number>=0;
}


void MergeSpaceCoordinator::relinquish() {
	if(held_lock_number>=0) {
		log(LOG_DEBUG,"MergeSpaceCoordinator::relinquish(): held_lock_number=%d", held_lock_number);
		ScopedLock sl(mtx);
		int tmp = held_lock_number;
		held_lock_number = -1;
		sl.unlock();
		std::string filename(make_lock_filename(lock_dir,tmp));
		int fd = open(filename.c_str(),O_WRONLY|O_TRUNC,0666);
		if(fd>=0)
			close(fd);
		else
			(void)::unlink(filename.c_str());
	}
}



void *hold_lock_thread_function_trampoline(void *pv) {
	MergeSpaceCoordinator *msc = (MergeSpaceCoordinator*)pv;
	msc->hold_lock_thread_function();
	return NULL;
}


//touch the lock file every 30 seconds
void MergeSpaceCoordinator::hold_lock_thread_function()
{
	ScopedLock sl(mtx);
	while(!please_shutdown) {
		//touch the lock file if we hold the lock
		if(held_lock_number>=0) {
			std::string filename(make_lock_filename(lock_dir,held_lock_number));
			struct timeval tv[2];
			gettimeofday(tv+0,NULL);
			tv[1] = tv[0];
			if(utimes(filename.c_str(),tv)!=0) {
				log(LOG_ERROR,"utimes(%s) failed with errno=%d (%s)", filename.c_str(), errno, strerror(errno));
			}
			
		}
		//sleep 30 seconds
		timespec ts;
		clock_gettime(CLOCK_REALTIME,&ts);
		ts.tv_sec += touch_interval;
		(void)pthread_cond_timedwait(&cond,&mtx.mtx,&ts);
	}
}
