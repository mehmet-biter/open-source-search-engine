#ifndef GB_JOBSCHEDULER_H
#define GB_JOBSCHEDULER_H

#include <inttypes.h>
#include <vector>
#include <map>


class BigFile;
class FileState;

//exit codes/reasons when the finish callback is called
enum job_exit_t {
	job_exit_normal,	        //job ran to completion (possibly with errors)
	job_exit_cancelled,             //job was cancelled (typically due to file being removed)
	job_exit_deadline,	        //job was cancelled due to deadline
	job_exit_program_exit	        //job was cancelled because program is shutting down
};


typedef void (*start_routine_t)(void *state);
typedef void (*finish_routine_t)(void *state, job_exit_t exit_type);


enum thread_type_t {
	thread_type_query_read,
	thread_type_query_constrain,
	thread_type_query_merge,
	thread_type_query_intersect,
	thread_type_query_summary,
	thread_type_spider_read,
	thread_type_spider_write,
	thread_type_spider_filter,      //pdf2html/doc2html/...
	thread_type_spider_query,       //?
	thread_type_merge_filter,
	thread_type_replicate_write,
	thread_type_replicate_read,
	thread_type_file_merge,
	thread_type_file_meta_data,     //unlink/rename
	thread_type_index_merge,
	thread_type_verify_data,        //mostly CPU
	thread_type_statistics,         //mostly i/o
	thread_type_unspecified_io,     //until we can be more specific
	thread_type_generate_thumbnail,
};



//A digest of a job in the scheduler. For statistics and display purposes
struct JobDigest {
	thread_type_t     thread_type;
	start_routine_t   start_routine;
	finish_routine_t  finish_callback;
	enum job_state_t {
		job_state_queued,
		job_state_running,
		job_state_stopped
	}                 job_state;
	uint64_t          start_deadline;     //latest time when this job must be started
	uint64_t          queue_enter_time;   //when this job was queued
	uint64_t          start_time;	      //when this job started running
	uint64_t          stop_time;	      //when this job stopped running
};


//statistics for queue and execution time per thread type
struct JobTypeStatistics {
	uint64_t job_count;
	uint64_t queue_time;            //time from enque to start, in nsecs
	uint64_t running_time;          //time from start to stop, in nsecs
	uint64_t done_time;             //time from stop to cleanup
	uint64_t cleanup_time;          //time from in cleanup
};


class JobScheduler_impl;

typedef void (*job_done_notify_t)();

class JobScheduler {
	JobScheduler(const JobScheduler&);
	JobScheduler& operator=(const JobScheduler&);
public:
	JobScheduler();
	~JobScheduler();
	
	bool initialize(unsigned num_cpu_threads, unsigned num_io_threads, unsigned num_external_threads, unsigned num_file_meta_threads, unsigned num_merge_threads, job_done_notify_t job_done_notify=0);
	void finalize();
	
	bool submit(start_routine_t   start_routine,
	            finish_routine_t  finish_callback,
		    void             *state,
		    thread_type_t     thread_type,
		    int               priority,
		    uint64_t          start_deadline=0);
	bool submit_io(start_routine_t   start_routine,
	               finish_routine_t  finish_callback,
		       FileState        *fstate,
		       thread_type_t     thread_type,
		       int               priority,
		       bool              is_write_job,
		       uint64_t          start_deadline=0);
	
	bool are_io_write_jobs_running() const;
	void cancel_file_read_jobs(const BigFile *bf);
	bool is_reading_file(const BigFile *bf);
	
	void allow_new_jobs();
	void disallow_new_jobs();
	bool are_new_jobs_allowed() const;
	
	unsigned num_queued_jobs() const;
	
	void cleanup_finished_jobs();
	
	std::vector<JobDigest> query_job_digests() const;
	std::map<thread_type_t,JobTypeStatistics> query_job_statistics(bool clear=true);

private:
	JobScheduler_impl *impl;
};

extern JobScheduler g_jobScheduler;

#endif // GB_JOBSCHEDULER_H
