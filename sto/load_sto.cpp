#include "sto.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <sys/resource.h>


int main(int argc, char **argv) {
	if(argc!=2 || strcmp(argv[1],"-?")==0 || strcmp(argv[1],"--help")==0) {
		fprintf(stderr,"usage: %s <sto-file>\n", argv[0]);
		return 1;
	}
	
	rusage ru_start; getrusage(RUSAGE_SELF,&ru_start);
	timespec ts_start; clock_gettime(CLOCK_REALTIME,&ts_start);
	
	for(int i=0; i<3; i++) {
		sto::Lexicon l;
		if(!l.load(argv[1])) {
			fprintf(stderr,"Could not open %s. Possible error: %s\n",argv[1],strerror(errno));
			return 2;
		}
	}
	
	rusage ru_end; getrusage(RUSAGE_SELF,&ru_end);
	timespec ts_end; clock_gettime(CLOCK_REALTIME,&ts_end);
	
	double user_time = ru_end.ru_utime.tv_sec-ru_start.ru_utime.tv_sec + (ru_end.ru_utime.tv_usec-ru_start.ru_utime.tv_usec)/1000000.0;
	double system_time = ru_end.ru_stime.tv_sec-ru_start.ru_stime.tv_sec + (ru_end.ru_stime.tv_usec-ru_start.ru_stime.tv_usec)/1000000.0;
	double wall_time = ts_end.tv_sec-ts_start.tv_sec + (ts_end.tv_nsec-ts_start.tv_nsec)/1000000000.0;
	printf("user time:   %.3f\n", user_time);
	printf("system time: %.3f\n", system_time);
	printf("wall time:   %.3f\n", wall_time);
	return 0;
}
