#include "MergeSpaceCoordinator.h"
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <signal.h>


#define LOCK_DIR "unittest_lock_dir"

static void cleanup() {
	char buf[1024];
	sprintf(buf,"rm -rf %s", LOCK_DIR);
	system(buf);
}

int main(void) {
	cleanup();
	
	printf("test simple instantiation\n");
	{
		MergeSpaceCoordinator msc(LOCK_DIR,3,"unittest_data_dir");
	}
	cleanup();
	
	printf("test simple invariant established by ctor\n");
	{
		MergeSpaceCoordinator msc(LOCK_DIR,3,"unittest_data_dir");
		assert(!msc.acquired());
	}
	cleanup();
	
	printf("test simple acquisition\n");
	{
		MergeSpaceCoordinator msc(LOCK_DIR,3,"unittest_data_dir");
		assert(msc.acquire(17));
		assert(msc.acquired());
		assert(access((std::string(LOCK_DIR)+"/lock0").c_str(),F_OK)==0);
		msc.relinquish();
	}
	cleanup();
	
	printf("test multiple acquisition\n");
	{
		MergeSpaceCoordinator msc0(LOCK_DIR,3,"unittest_data_dir");
		MergeSpaceCoordinator msc1(LOCK_DIR,3,"unittest_data_dir");
		MergeSpaceCoordinator msc2(LOCK_DIR,3,"unittest_data_dir");
		MergeSpaceCoordinator msc3(LOCK_DIR,3,"unittest_data_dir");
		assert(msc0.acquire(17));
		assert(msc0.acquired());
		assert(access((std::string(LOCK_DIR)+"/lock0").c_str(),F_OK)==0);
		assert(msc1.acquire(17));
		assert(msc1.acquired());
		assert(access((std::string(LOCK_DIR)+"/lock1").c_str(),F_OK)==0);
		assert(msc2.acquire(17));
		assert(msc2.acquired());
		assert(access((std::string(LOCK_DIR)+"/lock2").c_str(),F_OK)==0);
		assert(!msc3.acquire(17));
		assert(access((std::string(LOCK_DIR)+"/lock3").c_str(),F_OK)!=0);
		msc0.relinquish();
		msc1.relinquish();
		msc2.relinquish();
	}
	cleanup();
	
	printf("test junk in lock files\n");
	{
		mkdir(LOCK_DIR,0777);
		FILE *fp=fopen(LOCK_DIR "/lock0", "w");
		fprintf(fp,"banana");
		fclose(fp);
		MergeSpaceCoordinator msc(LOCK_DIR,1,"unittest_data_dir");
		assert(msc.acquire(17));
		msc.relinquish();
		fp = fopen(LOCK_DIR "/lock0", "r");
		char buf[ 20];
		fgets(buf,sizeof(buf),fp);
		fclose(fp);
		assert(strcmp(buf,"banana")!=0);
		assert(strtol(buf,0,0)>0);
	}
	cleanup();

	printf("test old pid in lock files\n");
	{
		int pid=0;
		while(kill(pid,0)==0)
			pid++;
		mkdir(LOCK_DIR,0777);
		FILE *fp=fopen(LOCK_DIR "/lock0", "w");
		fprintf(fp,"%d",pid);
		fclose(fp);
		MergeSpaceCoordinator msc(LOCK_DIR,1,"unittest_data_dir");
		assert(msc.acquire(17));
	}
	cleanup();

	printf("test existing pid in lock files\n");
	{
		int pid=0;
		while(kill(pid,0)!=0)
			pid++;
		mkdir(LOCK_DIR,0777);
		FILE *fp=fopen(LOCK_DIR "/lock0", "w");
		fprintf(fp,"%d",pid);
		fclose(fp);
		MergeSpaceCoordinator msc(LOCK_DIR,1,"unittest_data_dir");
		assert(!msc.acquire(17));
	}
	cleanup();


	printf("test lock holding. This takes more than a minute to test\n");
	{
		MergeSpaceCoordinator msc0(LOCK_DIR,1,"unittest_data_dir");
		assert(msc0.acquire(17));
		assert(msc0.acquired());
		//lock file must be touched every 30 seconds
		struct stat st0;
		assert(stat(LOCK_DIR "/lock0", &st0)==0);
		sleep(32);
		struct stat st1;
		assert(stat(LOCK_DIR "/lock0", &st1)==0);
		assert(st0.st_mtim.tv_sec < st1.st_mtim.tv_sec);
	}
	cleanup();
}
