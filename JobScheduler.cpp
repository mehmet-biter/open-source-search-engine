#include "JobScheduler.h"
#include "ScopedLock.h"
#include "BigFile.h" //for FileState definition
#include <pthread.h>
#include <vector>
#include <list>
#include <algorithm>
#include <stdexcept>
#include <assert.h>
#include <sys/time.h>


#include <stdio.h>

namespace {


//return current time expressed as milliseconds since 1970
static uint64_t now_ms() {
	struct timeval tv;
	gettimeofday(&tv,0);
	return tv.tv_sec*1000 + tv.tv_usec/1000;
}




//A job (queued, running or finished)
struct JobEntry {
	start_routine_t   start_routine;
	finish_routine_t  finish_callback;
	void             *state;
	
	bool              is_io_job;
	
	//for scheduling:
	uint64_t          start_deadline;     //latest time when this job must be started
	bool              is_io_write_job;    //valid for I/O jobs: mostly read or mostly write?
	int               initial_priority;   //priority when queued
	
	//for statistics:
	thread_type_t     thread_type;
	uint64_t          queue_enter_time;   //when this job was queued
	uint64_t          start_time;	      //when this job started running
	uint64_t          stop_time;	      //when this job stopped running
	uint64_t          finish_time;        //when the finish callback was called
	uint64_t          exit_time;	      //when this job was finished, including finish-callback
};




//a set of jobs, prioritized
class JobQueue : public std::vector<JobEntry> {
public:
	pthread_cond_t cond_not_empty;
	unsigned potential_worker_threads;

	JobQueue()
	  : vector(),
	    cond_not_empty PTHREAD_COND_INITIALIZER,
	    potential_worker_threads(0)
	{
	}
	
	~JobQueue() {
		pthread_cond_destroy(&cond_not_empty);
	}
	
	void add(const JobEntry &e) {
		push_back(e);
		pthread_cond_signal(&cond_not_empty);
	}
	
	JobEntry pop_top_priority();
	
	
};


JobEntry JobQueue::pop_top_priority()
{
	assert(!empty());
	//todo: age old entries
	std::vector<JobEntry>::iterator best_iter = begin();
	std::vector<JobEntry>::iterator iter = best_iter;
	++iter;
	for( ; iter!=end(); ++iter)
		if(iter->initial_priority<best_iter->initial_priority)
			best_iter = iter;
	JobEntry tmp = *best_iter;
	erase(best_iter);
	return tmp;
}



typedef std::vector<std::pair<JobEntry,job_exit_t>> ExitSet;
typedef std::list<JobEntry> RunningSet;


//parameters given to a pool thread
struct PoolThreadParameters {
	JobQueue          *job_queue;                 //queue to fetch jobs from
	RunningSet        *running_set;               //set to store the job in while executing it
	ExitSet           *exit_set;                  //set to store the finished job+exit-cause in
	unsigned          *num_io_write_jobs_running; //global counter for scheduling
	pthread_mutex_t   *mtx;                       //mutex covering above 3 containers
	job_done_notify_t job_done_notify;            //notifycation callback whenever a job returns
	bool              stop;
};


extern "C" {
static void *job_pool_thread_function(void *pv) {
	PoolThreadParameters *ptp= static_cast<PoolThreadParameters*>(pv);
	pthread_mutex_lock(ptp->mtx);
	while(!ptp->stop) {
		if(ptp->job_queue->empty())
			pthread_cond_wait(&ptp->job_queue->cond_not_empty,ptp->mtx);
		if(ptp->stop)
			break;
		if(ptp->job_queue->empty()) //spurious wakeup
			continue;
		
		//take the top-priority job and move it into the running set
		RunningSet::iterator iter = ptp->running_set->insert(ptp->running_set->begin(),ptp->job_queue->pop_top_priority());
		if(iter->is_io_job && iter->is_io_write_job)
			++*(ptp->num_io_write_jobs_running);
		pthread_mutex_unlock(ptp->mtx);
		
		job_exit_t job_exit;
		uint64_t now = now_ms();
		iter->start_time = now;
		if(iter->start_deadline==0 || iter->start_deadline>now) {
			// clear thread specific g_errno
			g_errno = 0;

			iter->start_routine(iter->state);
			iter->stop_time = now_ms();
			job_exit = job_exit_normal;
		} else {
			job_exit = job_exit_deadline;
		}
		
		pthread_mutex_lock(ptp->mtx);
		if(iter->is_io_job && iter->is_io_write_job)
			--*(ptp->num_io_write_jobs_running);
		//copy+delete it into the exit queue
		ptp->exit_set->push_back(std::make_pair(*iter,job_exit));
		ptp->running_set->erase(iter);
		pthread_mutex_unlock(ptp->mtx);
		
		(ptp->job_done_notify)();
		
		pthread_mutex_lock(ptp->mtx);
	}
	
	pthread_mutex_unlock(ptp->mtx);
	return 0;
}
}


static void job_done_notify_noop() {
}


class ThreadPool {
public:
	ThreadPool(const char *thread_name_prefix,
	           unsigned num_threads,
	           JobQueue *job_queue, RunningSet *running_set, ExitSet *exit_set,
		   unsigned *num_io_write_jobs_running,
		   pthread_mutex_t *mtx,
		   job_done_notify_t job_done_notify_);
	void initiate_stop();
	void join_all();
	~ThreadPool();
	
private:
	PoolThreadParameters ptp;
	std::vector<pthread_t> tid;
};


ThreadPool::ThreadPool(const char *thread_name_prefix,
                       unsigned num_threads,
                       JobQueue *job_queue, RunningSet *running_set, ExitSet *exit_set,
		       unsigned *num_io_write_jobs_running,
		       pthread_mutex_t *mtx,
                       job_done_notify_t job_done_notify_)
  : tid(num_threads)
{
	job_queue->potential_worker_threads += num_threads;
	ptp.job_queue = job_queue;
	ptp.running_set = running_set;
	ptp.exit_set = exit_set;
	ptp.num_io_write_jobs_running = num_io_write_jobs_running;
	ptp.mtx = mtx;
	ptp.job_done_notify = job_done_notify_?job_done_notify_:job_done_notify_noop;
	ptp.stop = false;
	for(unsigned i=0; i<tid.size(); i++) {
		int rc = pthread_create(&tid[i], NULL, job_pool_thread_function, &ptp);
		if(rc!=0)
			throw std::runtime_error("pthread_create() failed");
		char thread_name[16]; //hard limit
		snprintf(thread_name,sizeof(thread_name),"%s %u", thread_name_prefix,i);
		rc = pthread_setname_np(tid[i],thread_name);
		if(rc!=0)
			throw std::runtime_error("pthread_setname_np() failed");
	}
}


void ThreadPool::initiate_stop()
{
	for(unsigned i=0; i<tid.size(); i++)
		ptp.stop = true;
}

void ThreadPool::join_all()
{
	for(unsigned i=0; i<tid.size(); i++)
		pthread_join(tid[i],NULL);
	tid.clear();
}

ThreadPool::~ThreadPool()
{
	join_all();
}

} //anonymous namespace



////////////////////////////////////////////////////////////////////////////////
// JobScheduler implementation

class JobScheduler_impl {
	mutable pthread_mutex_t mtx;
	
	JobQueue   cpu_job_queue;
	JobQueue   io_job_queue;
	JobQueue   external_job_queue;
	JobQueue   file_meta_job_queue;
	JobQueue   merge_job_queue;
	
	RunningSet running_set;
	
	ExitSet    exit_set;
	
	unsigned   num_io_write_jobs_running;
	
	ThreadPool cpu_thread_pool;
	ThreadPool io_thread_pool;
	ThreadPool external_thread_pool;
	ThreadPool file_meta_thread_pool;
	ThreadPool merge_thread_pool;
	
	bool no_threads;
	bool new_jobs_allowed;
	
	std::map<thread_type_t,JobTypeStatistics> job_statistics;
	
	bool submit(thread_type_t thread_type, JobEntry &e);
	
	void cancel_queued_jobs(JobQueue &jq, job_exit_t job_exit);
public:
	JobScheduler_impl(unsigned num_cpu_threads, unsigned num_io_threads, unsigned num_external_threads, unsigned num_file_meta_threads, unsigned num_merge_threads, job_done_notify_t job_done_notify)
	  : mtx PTHREAD_MUTEX_INITIALIZER,
	    cpu_job_queue(),
	    io_job_queue(),
	    external_job_queue(),
	    file_meta_job_queue(),
	    merge_job_queue(),
	    running_set(),
	    exit_set(),
	    num_io_write_jobs_running(0),
	    cpu_thread_pool("cpu",num_cpu_threads,&cpu_job_queue,&running_set,&exit_set,&num_io_write_jobs_running,&mtx,job_done_notify),
	    io_thread_pool("io",num_io_threads,&io_job_queue,&running_set,&exit_set,&num_io_write_jobs_running,&mtx,job_done_notify),
	    external_thread_pool("ext",num_external_threads,&external_job_queue,&running_set,&exit_set,&num_io_write_jobs_running,&mtx,job_done_notify),
	    file_meta_thread_pool("file",num_file_meta_threads,&file_meta_job_queue,&running_set,&exit_set,&num_io_write_jobs_running,&mtx,job_done_notify),
	    merge_thread_pool("merge",num_merge_threads,&merge_job_queue,&running_set,&exit_set,&num_io_write_jobs_running,&mtx,job_done_notify),
	    no_threads(num_cpu_threads==0 && num_io_threads==0 && num_external_threads==0 && num_file_meta_threads==0),
	    new_jobs_allowed(true)
	{
	}
	
	~JobScheduler_impl();
	
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
	//void nice page for html and administation()
	bool is_reading_file(const BigFile *bf);
	
	void allow_new_jobs() {
		new_jobs_allowed = true;
	}
	void disallow_new_jobs() {
		new_jobs_allowed = false;
	}
	bool are_new_jobs_allowed() const {
		return new_jobs_allowed && !no_threads;
	}
	void cancel_all_jobs_for_shutdown();
	
	unsigned num_queued_jobs() const;
	
	void cleanup_finished_jobs();
	
	std::vector<JobDigest> query_job_digests() const;
	std::map<thread_type_t,JobTypeStatistics> query_job_statistics(bool clear);
};



JobScheduler_impl::~JobScheduler_impl() {
	//First prevent new jobs from being submitted
	new_jobs_allowed = false;

	//Then tell the worker threads to stop executing more jobs
	cpu_thread_pool.initiate_stop();
	io_thread_pool.initiate_stop();
	external_thread_pool.initiate_stop();
	file_meta_thread_pool.initiate_stop();
	merge_thread_pool.initiate_stop();

	//Then wake them if they are sleeping
	pthread_cond_broadcast(&cpu_job_queue.cond_not_empty);
	pthread_cond_broadcast(&io_job_queue.cond_not_empty);
	pthread_cond_broadcast(&external_job_queue.cond_not_empty);
	pthread_cond_broadcast(&file_meta_job_queue.cond_not_empty);
	pthread_cond_broadcast(&merge_job_queue.cond_not_empty);

	//Then cancel all outstanding non-started jobs by moving them from the pending queues to the exit-set
	{
		ScopedLock sl(mtx);
		cancel_queued_jobs(cpu_job_queue,job_exit_program_exit);
		cancel_queued_jobs(io_job_queue,job_exit_program_exit);
		cancel_queued_jobs(external_job_queue,job_exit_program_exit);
		cancel_queued_jobs(file_meta_job_queue,job_exit_program_exit);
		cancel_queued_jobs(merge_job_queue,job_exit_program_exit);
	}

	//Call finish-callbacks for all the exited / cancelled threads
	//We "know" that this function (finalize) is only called from the main thread, *cough* *cough*
	cleanup_finished_jobs();
	
	//Then wait  for worker threads to finished
	cpu_thread_pool.join_all();
	io_thread_pool.join_all();
	file_meta_thread_pool.join_all();
	merge_thread_pool.join_all();
	external_thread_pool.join_all();

	pthread_mutex_destroy(&mtx);
}


bool JobScheduler_impl::submit(thread_type_t thread_type, JobEntry &e)
{
	if(!new_jobs_allowed) //note: unprotected read
		return false;
	e.thread_type = thread_type;
	
	//Determine which queue to put the job into
	//i/o jobs should have the is_io_job=true, but if they don't we will
	//just treat them as CPU-bound. All this looks over-engineered but we
	//need some flexibility to make experiments.
	JobQueue *job_queue;
	if(e.is_io_job)
		job_queue = &io_job_queue;
	else {
		switch(thread_type) {
			case thread_type_query_read:         job_queue = &cpu_job_queue;      break;
			case thread_type_query_constrain:    job_queue = &cpu_job_queue;      break;
			case thread_type_query_merge:        job_queue = &cpu_job_queue;      break;
			case thread_type_query_intersect:    job_queue = &cpu_job_queue;      break;
			case thread_type_query_summary:      job_queue = &cpu_job_queue;      break;
			case thread_type_spider_read:        job_queue = &cpu_job_queue;      break;
			case thread_type_spider_write:       job_queue = &cpu_job_queue;      break;
			case thread_type_spider_filter:      job_queue = &external_job_queue; break;
			case thread_type_spider_query:       job_queue = &cpu_job_queue;      break;
			case thread_type_spider_index:       job_queue = &cpu_job_queue;      break;
			case thread_type_merge_filter:       job_queue = &merge_job_queue;      break;
			case thread_type_replicate_write:    job_queue = &cpu_job_queue;      break;
			case thread_type_replicate_read:     job_queue = &cpu_job_queue;      break;
			case thread_type_file_merge:         job_queue = &merge_job_queue;    break;
			case thread_type_file_meta_data:     job_queue = &file_meta_job_queue;break;
			case thread_type_index_merge:        job_queue = &cpu_job_queue;      break;
			case thread_type_index_generate:     job_queue = &merge_job_queue;    break;
			case thread_type_verify_data:        job_queue = &cpu_job_queue;      break;
			case thread_type_statistics:         job_queue = &cpu_job_queue;      break;
			case thread_type_unspecified_io:     job_queue = &cpu_job_queue;      break;
			case thread_type_generate_thumbnail: job_queue = &external_job_queue; break;
			default:
				assert(false);

		}
	}
	
	if(job_queue->potential_worker_threads==0)
		return false;
	
	e.queue_enter_time = now_ms();
	ScopedLock sl(mtx);
	job_queue->add(e);
	return true;
}


void JobScheduler_impl::cancel_queued_jobs(JobQueue &jq, job_exit_t job_exit) {
	while(!jq.empty()) {
		exit_set.push_back(std::make_pair(jq.back(),job_exit));
		jq.pop_back();
	}
}


bool JobScheduler_impl::submit(start_routine_t   start_routine,
                               finish_routine_t  finish_callback,
                               void             *state,
                               thread_type_t     thread_type,
                               int               priority,
                               uint64_t          start_deadline)
{
	JobEntry e;
	
	memset(&e, 0, sizeof(JobEntry));
	e.start_routine = start_routine;
	e.finish_callback = finish_callback;
	e.state = state;
	e.is_io_job = false;
	e.start_deadline = start_deadline;
	e.is_io_write_job = false;
	e.initial_priority = priority;
	return submit(thread_type,e);
}

bool JobScheduler_impl::submit_io(start_routine_t   start_routine,
                                  finish_routine_t  finish_callback,
                                  FileState        *fstate,
                                  thread_type_t     thread_type,
                                  int               priority,
                                  bool              is_write_job,
                                  uint64_t          start_deadline)
{
	JobEntry e;
	
	memset(&e, 0, sizeof(JobEntry));
	e.start_routine = start_routine;
	e.finish_callback = finish_callback;
	e.state = fstate;
	e.is_io_job = true;
	e.start_deadline = start_deadline;
	e.is_io_write_job = is_write_job;
	e.initial_priority = priority;
	return submit(thread_type,e);
}




bool JobScheduler_impl::are_io_write_jobs_running() const
{
	ScopedLock sl(mtx);
	return num_io_write_jobs_running != 0;
}



void JobScheduler_impl::cancel_file_read_jobs(const BigFile *bf)
{
	ScopedLock sl(mtx);
	for(JobQueue::iterator iter = io_job_queue.begin(); iter!=io_job_queue.end(); ) {
		if(iter->is_io_job && !iter->is_io_write_job) {
			const FileState *fstate = reinterpret_cast<const FileState*>(iter->state);
			if(fstate->m_bigfile==bf) {
				exit_set.push_back(std::make_pair(*iter,job_exit_cancelled));
				iter = io_job_queue.erase(iter);
				continue;
			}
		}
		++iter;
	}
}


bool JobScheduler_impl::is_reading_file(const BigFile *bf)
{
	//The old thread stuff tested explicitly if the start_routine was
	//readwriteWrapper_r() in BigFile.cpp but that is fragile. Besides,
	//we have the 'is_io_write_job' field.
	ScopedLock sl(mtx);
	for(const auto &e : io_job_queue) {
		if(e.is_io_job && !e.is_io_write_job) {
			const FileState *fstate = reinterpret_cast<const FileState*>(e.state);
			if(fstate->m_bigfile==bf)
				return true;
		}
	}
	for(const auto &e : running_set) {
		if(e.is_io_job && !e.is_io_write_job) {
			const FileState *fstate = reinterpret_cast<const FileState*>(e.state);
			if(fstate->m_bigfile==bf)
				return true;
		}
	}
	return false;
}


void JobScheduler_impl::cancel_all_jobs_for_shutdown() {
	ScopedLock sl(mtx);
	cancel_queued_jobs(cpu_job_queue,job_exit_program_exit);
	cancel_queued_jobs(io_job_queue,job_exit_program_exit);
	cancel_queued_jobs(external_job_queue,job_exit_program_exit);
	cancel_queued_jobs(file_meta_job_queue,job_exit_program_exit);
	cancel_queued_jobs(merge_job_queue,job_exit_program_exit);
}



unsigned JobScheduler_impl::num_queued_jobs() const
{
	ScopedLock sl(mtx);
	return cpu_job_queue.size() + io_job_queue.size() + external_job_queue.size() + file_meta_job_queue.size() + merge_job_queue.size();
}

void JobScheduler_impl::cleanup_finished_jobs()
{
	ExitSet es;
	ScopedLock sl(mtx);
	es.swap(exit_set);
	sl.unlock();
	
	for(auto e : es) {
		e.first.finish_time = now_ms();
		if(e.first.finish_callback)
			e.first.finish_callback(e.first.state,e.second);
		e.first.exit_time = now_ms();
		
		JobTypeStatistics &s = job_statistics[e.first.thread_type];
		s.job_count++;
		s.queue_time += e.first.start_time - e.first.queue_enter_time;
		s.running_time += e.first.stop_time - e.first.start_time;
		s.done_time += e.first.finish_time - e.first.stop_time;
		s.cleanup_time += e.first.exit_time - e.first.finish_time;
	}
}


static JobDigest job_entry_to_job_digest(const JobEntry& je, JobDigest::job_state_t job_state)
{
	JobDigest jd;
	jd.thread_type = je.thread_type;
	jd.start_routine = je.start_routine;
	jd.finish_callback = je.finish_callback;
	jd.job_state = job_state;
	jd.start_deadline = je.start_deadline;
	jd.queue_enter_time = je.queue_enter_time;
	jd.start_time = je.start_time;
	jd.stop_time = je.stop_time;
	return jd;
}


std::vector<JobDigest> JobScheduler_impl::query_job_digests() const
{
	std::vector<JobDigest> v;
	ScopedLock sl(mtx);
	//v.reserve(cpu_job_queue.size() + io_job_queue.size() + external_job_queue.size() + file_meta_job_queue.size() + running_set().size() + exit_set().size());
	for(const auto &je : cpu_job_queue)
		v.push_back(job_entry_to_job_digest(je,JobDigest::job_state_queued));
	for(const auto &je : io_job_queue)
		v.push_back(job_entry_to_job_digest(je,JobDigest::job_state_queued));
	for(const auto &je : external_job_queue)
		v.push_back(job_entry_to_job_digest(je,JobDigest::job_state_queued));
	for(const auto &je : file_meta_job_queue)
		v.push_back(job_entry_to_job_digest(je,JobDigest::job_state_queued));
	for(const auto &je : merge_job_queue)
		v.push_back(job_entry_to_job_digest(je,JobDigest::job_state_queued));
	for(const auto &je : running_set)
		v.push_back(job_entry_to_job_digest(je,JobDigest::job_state_running));
	for(const auto &je : exit_set)
		v.push_back(job_entry_to_job_digest(je.first,JobDigest::job_state_stopped));
	return v;
	
}


std::map<thread_type_t,JobTypeStatistics> JobScheduler_impl::query_job_statistics(bool clear)
{
	if(clear) {
		std::map<thread_type_t,JobTypeStatistics> tmp;
		tmp.swap(job_statistics);
		return tmp;
	} else
		return job_statistics;
}




////////////////////////////////////////////////////////////////////////////////
// bridge

JobScheduler::JobScheduler()
  : impl(0)
{
}


JobScheduler::~JobScheduler()
{
	delete impl;
	impl = 0;
}


bool JobScheduler::initialize(unsigned num_cpu_threads, unsigned num_io_threads, unsigned num_external_threads, unsigned num_file_meta_threads, unsigned num_merge_threads, job_done_notify_t job_done_notify)
{
	assert(!impl);
	impl = new JobScheduler_impl(num_cpu_threads,num_io_threads,num_external_threads,num_file_meta_threads,num_merge_threads,job_done_notify);
	return true;
}


void JobScheduler::finalize()
{
	assert(impl);
	delete impl;
	impl = 0;
}


bool JobScheduler::submit(start_routine_t   start_routine,
                          finish_routine_t  finish_callback,
                          void             *state,
                          thread_type_t     thread_type,
                          int               priority,
                          uint64_t          start_deadline)
{
	if(impl)
		return impl->submit(start_routine,finish_callback,state,thread_type,priority,start_deadline);
	else
		return false;
}

bool JobScheduler::submit_io(start_routine_t   start_routine,
                          finish_routine_t  finish_callback,
                          FileState        *fstate,
                          thread_type_t     thread_type,
                          int               priority,
                          bool              is_write_job,
                          uint64_t          start_deadline)
{
	if(impl)
		return impl->submit_io(start_routine,finish_callback,fstate,thread_type,priority,is_write_job,start_deadline);
	else
		return false;
}


bool JobScheduler::are_io_write_jobs_running() const
{
	if(impl)
		return impl->are_io_write_jobs_running();
	else
		return false;
}


void JobScheduler::cancel_file_read_jobs(const BigFile *bf)
{
	if(impl)
		impl->cancel_file_read_jobs(bf);
}


//void nice page for html and administation()


bool JobScheduler::is_reading_file(const BigFile *bf)
{
	if(impl)
		return impl->is_reading_file(bf);
	else
		return false;
}


void JobScheduler::allow_new_jobs()
{
	if(impl)
		impl->allow_new_jobs();
}

void JobScheduler::disallow_new_jobs()
{
	if(impl)
		impl->disallow_new_jobs();
}

bool JobScheduler::are_new_jobs_allowed() const
{
	if(impl)
		return impl->are_new_jobs_allowed();
	else
		return false;
}


void JobScheduler::cancel_all_jobs_for_shutdown() {
	if(impl)
		impl->cancel_all_jobs_for_shutdown();
}


unsigned JobScheduler::num_queued_jobs() const
{
	if(impl)
		return impl->num_queued_jobs();
	else
		return 0;
}


void JobScheduler::cleanup_finished_jobs()
{
	if(impl)
		impl->cleanup_finished_jobs();
}


std::vector<JobDigest> JobScheduler::query_job_digests() const
{
	if(impl)
		return impl->query_job_digests();
	else
		return std::vector<JobDigest>();
}


std::map<thread_type_t,JobTypeStatistics> JobScheduler::query_job_statistics(bool clear)
{
	if(impl)
		return impl->query_job_statistics(clear);
	else
		return std::map<thread_type_t,JobTypeStatistics>();
}


////////////////////////////////////////////////////////////////////////////////
// The global one-and-only scheduler

JobScheduler g_jobScheduler;
