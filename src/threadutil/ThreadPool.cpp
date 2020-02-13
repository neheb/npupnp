/*******************************************************************************
 *
 * Copyright (c) 2000-2003 Intel Corporation 
 * All rights reserved. 
 * Copyright (c) 2012 France Telecom All rights reserved. 
 * Copyright (c) 2020 J.F. Dockes <jf@dockes.org>
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions are met: 
 *
 * - Redistributions of source code must retain the above copyright notice, 
 * this list of conditions and the following disclaimer. 
 * - Redistributions in binary form must reproduce the above copyright notice, 
 * this list of conditions and the following disclaimer in the documentation 
 * and/or other materials provided with the distribution. 
 * - Neither name of Intel Corporation nor the names of its contributors 
 * may be used to endorse or promote products derived from this software 
 * without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL OR 
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY 
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS 
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 ******************************************************************************/

#include "ThreadPool.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>	/* for memset()*/

#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <iostream>

using namespace std::chrono;

/*! Internal ThreadPool Job. */
struct ThreadPoolJob {
	ThreadPoolJob(start_routine _func,
				  void *_arg, ThreadPool::free_routine _frfunc,
				  ThreadPool::ThreadPriority _prio)
		: func(_func), arg(_arg), free_func(_frfunc), priority(_prio) {
	}
	~ThreadPoolJob() {
		if (free_func)
			free_func(arg);
	}
	start_routine func;
	void *arg;
	ThreadPool::free_routine free_func;
	ThreadPool::ThreadPriority priority;
	steady_clock::time_point requestTime;
	int jobId;
};

class ThreadPool::Internal {
public:
	Internal(ThreadPoolAttr *attr);
	bool ok{false};
	int createWorker(std::unique_lock<std::mutex>& lck);
	void addWorker(std::unique_lock<std::mutex>& lck);
	void StatsAccountLQ(long diffTime);
	void StatsAccountMQ(long diffTime);
	void StatsAccountHQ(long diffTime);
	void CalcWaitTime(ThreadPriority p, ThreadPoolJob *job);
	void bumpPriority();
	int shutdown();
	
	/*! Mutex to protect job qs. */
	std::mutex mutex;
	/*! Condition variable to signal Q. */
	std::condition_variable condition;
	/*! Condition variable for start and stop. */
	std::condition_variable start_and_shutdown;

	/*! ids for jobs */
	int lastJobId;
	/*! whether or not we are shutting down */
	bool shuttingdown;
	/*! total number of threads */
	int totalThreads;
	/*! flag that's set when waiting for a new worker thread to start */
	int pendingWorkerThreadStart;
	/*! number of threads that are currently executing jobs */
	int busyThreads;
	/*! number of persistent threads */
	int persistentThreads;
	/*! low priority job Q */
	std::list<ThreadPoolJob*> lowJobQ;
	/*! med priority job Q */
	std::list<ThreadPoolJob*> medJobQ;
	/*! high priority job Q */
	std::list<ThreadPoolJob*> highJobQ;
	/*! persistent job */
	ThreadPoolJob *persistentJob;
	/*! thread pool attributes */
	ThreadPoolAttr attr;
	/*! statistics */
	ThreadPoolStats stats;
};


ThreadPool::ThreadPool()
	: m{nullptr}
{
}

ThreadPool::~ThreadPool()
{
	// JFD: Doing a proper shutdown does not work at the moment. One
	// of the threads does not exit. I suspect it's the timer thread
	// (not quite sure), but we have no way to signal it. For this
	// stuff to work, any permanent thread should poll for an exit
	// event, not the case at this point. I suspect that the original
	// design is wrong: the persistent threads are probably not
	// compatible with the shutdown() routine.  This is no big deal,
	// because I can't think of a process which would want to shutdown
	// its UPnP service and do something else further on... Going to
	// exit anyway. Actually calling _exit() might be the smart thing here :)
#if 0
	shutdown();
	delete m;
#endif
}

int ThreadPool::start(ThreadPoolAttr *attr)
{
	m = new Internal(attr);
	if (m && m->ok) {
		return 0;
	}
	return -1;
}

void ThreadPool::Internal::StatsAccountLQ(long diffTime)
{
	this->stats.totalJobsLQ++;
	this->stats.totalTimeLQ += (double)diffTime;
}

void ThreadPool::Internal::StatsAccountMQ(long diffTime)
{
	this->stats.totalJobsMQ++;
	this->stats.totalTimeMQ += (double)diffTime;
}

void ThreadPool::Internal::StatsAccountHQ(long diffTime)
{
	this->stats.totalJobsHQ++;
	this->stats.totalTimeHQ += (double)diffTime;
}

/*!
 * \brief Calculates the time the job has been waiting at the specified
 * priority.
 *
 * Adds to the totalTime and totalJobs kept in the thread pool statistics
 * structure.
 *
 * \internal
 */
void ThreadPool::Internal::CalcWaitTime(ThreadPriority p, ThreadPoolJob *job)
{
	assert(job != NULL);

	auto now = steady_clock::now();
    auto ms =
        duration_cast<milliseconds>(now - job->requestTime);
	long diff = ms.count();
	switch (p) {
	case LOW_PRIORITY:
		StatsAccountLQ(diff);
		break;
	case MED_PRIORITY:
		StatsAccountMQ(diff);
		break;
	case HIGH_PRIORITY:
		StatsAccountHQ(diff);
		break;
	default:
		assert(0);
	}
}

/*!
 * \brief Sets the scheduling policy of the current process.
 *
 * \internal
 * 
 * \return
 * 	\li \c 0 on success.
 *      \li \c result of GetLastError() on failure.
 *
 */
static int SetPolicyType(ThreadPoolAttr::PolicyType in)
{
	int retVal = 0;
#ifdef __CYGWIN__
	/* TODO not currently working... */
	retVal = 0;
#elif defined(__OSX__) || defined(__APPLE__) || defined(__NetBSD__)
	setpriority(PRIO_PROCESS, 0, 0);
	retVal = 0;
#elif defined(_MSC_VER)
	retVal = sched_setscheduler(0, in);
#elif defined(_POSIX_PRIORITY_SCHEDULING) && _POSIX_PRIORITY_SCHEDULING > 0
	struct sched_param current;
	int sched_result;

	memset(&current, 0, sizeof(current));
	sched_getparam(0, &current);
	current.sched_priority = sched_get_priority_min(DEFAULT_POLICY);
	sched_result = sched_setscheduler(0, in, &current);
	retVal = (sched_result != -1 || errno == EPERM) ? 0 : errno;
#else
	retVal = 0;
#endif
	return retVal;
}

/*!
 * \brief Sets the priority of the currently running thread.
 *
 * \internal
 * 
 * \return
 *	\li \c 0 on success.
 *      \li \c EINVAL invalid priority or the result of GerLastError.
 */
static int SetPriority(ThreadPool::ThreadPriority priority)
{
#if defined(_POSIX_PRIORITY_SCHEDULING) && _POSIX_PRIORITY_SCHEDULING > 0
	int retVal = 0;
	int currentPolicy;
	int minPriority = 0;
	int maxPriority = 0;
	int actPriority = 0;
	int midPriority = 0;
	struct sched_param newPriority;
	int sched_result;

	pthread_getschedparam(ithread_self(), &currentPolicy, &newPriority);
	minPriority = sched_get_priority_min(currentPolicy);
	maxPriority = sched_get_priority_max(currentPolicy);
	midPriority = (maxPriority - minPriority) / 2;
	switch (priority) {
	case LOW_PRIORITY:
		actPriority = minPriority;
		break;
	case MED_PRIORITY:
		actPriority = midPriority;
		break;
	case HIGH_PRIORITY:
		actPriority = maxPriority;
		break;
	default:
		retVal = EINVAL;
		goto exit_function;
	};

	newPriority.sched_priority = actPriority;

	sched_result = pthread_setschedparam(ithread_self(), currentPolicy, &newPriority);
	retVal = (sched_result == 0 || errno == EPERM) ? 0 : sched_result;
exit_function:
	return retVal;
#else
	return 0;
	priority = priority;
#endif
}

/*!
 * \brief Determines whether any jobs need to be bumped to a higher priority Q
 * and bumps them.
 *
 * tp->mutex must be locked.
 *
 * \internal
 * 
 * \return
 */
void ThreadPool::Internal::bumpPriority()
{
	int done = 0;
	ThreadPoolJob *tempJob = NULL;

	auto now = steady_clock::now();
	while (!done) {
		if (this->medJobQ.size()) {
			tempJob = this->medJobQ.front();
			long diffTime = duration_cast<milliseconds>(
				now - tempJob->requestTime).count();
			if (diffTime >= this->attr.starvationTime) {
				/* If job has waited longer than the starvation time
				* bump priority (add to higher priority Q) */
				StatsAccountMQ(diffTime);
				this->medJobQ.pop_front();
				this->highJobQ.push_back(tempJob);
				continue;
			}
		}
		if (this->lowJobQ.size()) {
			tempJob = this->lowJobQ.front();
			long diffTime = duration_cast<milliseconds>(
				now - tempJob->requestTime).count();
			if (diffTime >= this->attr.maxIdleTime) {
				/* If job has waited longer than the starvation time
				 * bump priority (add to higher priority Q) */
				StatsAccountLQ(diffTime);
				this->lowJobQ.pop_front();
				this->medJobQ.push_back(tempJob);
				continue;
			}
		}
		done = 1;
	}
}

/*
 * \brief Sets seed for random number generator. Each thread sets the seed
 * random number generator. */
static void SetSeed(void)
{
	const auto p1 = std::chrono::system_clock::now();
	auto cnt = p1.time_since_epoch().count();
	// Keep the nanoseconds
	cnt = cnt % 1000000000;
	std::hash<std::thread::id> id_hash;
	size_t h = id_hash(std::this_thread::get_id());
	srand((unsigned int)(cnt+h));
}

/*!
 * \brief Implements a thread pool worker. Worker waits for a job to become
 * available. Worker picks up persistent jobs first, high priority,
 * med priority, then low priority.
 *
 * If worker remains idle for more than specified max, the worker is released.
 *
 *! arg -> is cast to (ThreadPool::Internal *).
 */
static void *WorkerThread(void *arg)
{
	ThreadPool::Internal *tp = (ThreadPool::Internal *)arg;
	time_t start = 0;
	ThreadPoolJob *job = NULL;
	std::cv_status retCode;
	int persistent = -1;

	std::unique_lock<std::mutex> lck(tp->mutex, std::defer_lock);
	auto idlemillis = std::chrono::milliseconds(tp->attr.maxIdleTime);

	/* Increment total thread count */
	lck.lock();
	tp->totalThreads++;
	tp->pendingWorkerThreadStart = 0;
	tp->start_and_shutdown.notify_all();
	lck.unlock();
	
	SetSeed();
	start = time(nullptr);
	while (1) {
		lck.lock();
		if (job) {
			tp->busyThreads--;
			delete job;
			job = NULL;
		}
		tp->stats.idleThreads++;
		tp->stats.totalWorkTime += (double)time(nullptr) - (double)start;
		start = time(nullptr);
		if (persistent == 0) {
			tp->stats.workerThreads--;
		} else if (persistent == 1) {
			/* Persistent thread becomes a regular thread */
			tp->persistentThreads--;
		}

		/* Check for a job or shutdown */
		retCode = std::cv_status::no_timeout;
		while (tp->lowJobQ.size() == 0 &&
		       tp->medJobQ.size()  == 0 &&
		       tp->highJobQ.size() == 0 &&
		       !tp->persistentJob && !tp->shuttingdown) {
			/* If wait timed out and we currently have more than the
			 * min threads, or if we have more than the max threads
			 * (only possible if the attributes have been reset)
			 * let this thread die. */
			if ((retCode == std::cv_status::timeout &&
				 tp->totalThreads > tp->attr.minThreads) ||
			    (tp->attr.maxThreads != -1 &&
			     tp->totalThreads > tp->attr.maxThreads)) {
				tp->stats.idleThreads--;
				goto exit_function;
			}

			/* wait for a job up to the specified max time */
			retCode = tp->condition.wait_for(lck, idlemillis);
		}
		tp->stats.idleThreads--;
		/* idle time */
		tp->stats.totalIdleTime += (double)time(nullptr) - (double)start;
		/* work time */
		start = time(nullptr);
		/* bump priority of starved jobs */
		tp->bumpPriority();
		/* if shutdown then stop */
		if (tp->shuttingdown) {
			goto exit_function;
		} else {
			/* Pick up persistent job if available */
			if (tp->persistentJob) {
				job = tp->persistentJob;
				tp->persistentJob = NULL;
				tp->persistentThreads++;
				persistent = 1;
				tp->start_and_shutdown.notify_all();
			} else {
				tp->stats.workerThreads++;
				persistent = 0;
				/* Pick the highest priority job */
				if (tp->highJobQ.size() > 0) {
					job = tp->highJobQ.front();
					tp->highJobQ.pop_front();
					tp->CalcWaitTime(ThreadPool::HIGH_PRIORITY, job);
				} else if (tp->medJobQ.size() > 0) {
					job = tp->medJobQ.front();
					tp->medJobQ.pop_front();
					tp->CalcWaitTime(ThreadPool::MED_PRIORITY, job);
				} else if (tp->lowJobQ.size() > 0) {
					job = tp->lowJobQ.front();
					tp->lowJobQ.pop_front();
					tp->CalcWaitTime(ThreadPool::LOW_PRIORITY, job);
				} else {
					/* Should never get here */
					tp->stats.workerThreads--;
					goto exit_function;
				}
			}
		}

		tp->busyThreads++;
		lck.unlock();

		SetPriority(job->priority);
		/* run the job */
		job->func(job->arg);
		/* return to Normal */
		SetPriority(ThreadPool::MED_PRIORITY);
	}

exit_function:
	tp->totalThreads--;
	tp->start_and_shutdown.notify_all();
	return NULL;
}

/*!
 * \brief Creates a worker thread, if the thread pool does not already have
 * max threads.
 *
 * \remark The ThreadPool object mutex must be locked prior to calling this
 * function.
 *
 * \internal
 *
 * \return
 *	\li \c 0 on success, < 0 on failure.
 *	\li \c EMAXTHREADS if already max threads reached.
 *	\li \c EAGAIN if system can not create thread.
 */
int ThreadPool::Internal::createWorker(std::unique_lock<std::mutex>& lck)
{
	/* if a new worker is the process of starting, wait until it fully starts */
	while (this->pendingWorkerThreadStart) {
		this->start_and_shutdown.wait(lck);
	}

	if (this->attr.maxThreads != ThreadPoolAttr::INFINITE_THREADS &&
	    this->totalThreads + 1 > this->attr.maxThreads) {
		return EMAXTHREADS;
	}

	std::thread nthread(WorkerThread, this);
	nthread.detach();

	/* wait until the new worker thread starts. We can set the flag
	   cause we have the lock */
	this->pendingWorkerThreadStart = 1;
	while (this->pendingWorkerThreadStart) {
		this->start_and_shutdown.wait(lck);
	}

	if (this->stats.maxThreads < this->totalThreads) {
		this->stats.maxThreads = this->totalThreads;
	}

	return 0;
}

/*!
 * \brief Determines whether or not a thread should be added based on the
 * jobsPerThread ratio. Adds a thread if appropriate.
 *
 * \remark The ThreadPool object mutex must be locked prior to calling this
 * function.
 *
 */
void ThreadPool::Internal::addWorker(std::unique_lock<std::mutex>& lck)
{
	long jobs = highJobQ.size() + lowJobQ.size() + medJobQ.size();
	int threads = totalThreads - persistentThreads;
	while (threads == 0 ||
	       (jobs / threads) >= attr.jobsPerThread ||
	       (totalThreads == busyThreads) ) {
		if (createWorker(lck) != 0) {
			return;
		}
		threads++;
	}
}

ThreadPool::Internal::Internal(ThreadPoolAttr *attr)
{
	int retCode = 0;
	int i = 0;

	std::unique_lock<std::mutex> lck(this->mutex);
	if (attr) {
		this->attr = *attr;
	}
	if (SetPolicyType(this->attr.schedPolicy) != 0) {
		return;
	}
	this->stats = ThreadPoolStats();
	this->persistentJob = NULL;
	this->lastJobId = 0;
	this->shuttingdown = 0;
	this->totalThreads = 0;
	this->busyThreads = 0;
	this->persistentThreads = 0;
	this->pendingWorkerThreadStart = 0;
	for (i = 0; i < this->attr.minThreads; ++i) {
		retCode = createWorker(lck);
		if (retCode) {
			break;
		}
	}

	lck.unlock();

	if (retCode) {
		/* clean up if the min threads could not be created */
		this->shutdown();
	} else {
		ok = true;
	}
}

int ThreadPool::addPersistent(start_routine func, void *arg, 
					  free_routine free_func,
					  ThreadPriority priority)
{
	int ret = 0;
	ThreadPoolJob *job = NULL;

	std::unique_lock<std::mutex> lck(m->mutex);

	/* Create A worker if less than max threads running */
	if (m->totalThreads < m->attr.maxThreads) {
		m->createWorker(lck);
	} else {
		/* if there is more than one worker thread
		 * available then schedule job, otherwise fail */
		if (m->totalThreads - m->persistentThreads - 1 == 0) {
			ret = EMAXTHREADS;
			goto exit_function;
		}
	}
	
	job = new ThreadPoolJob(func, arg, free_func, priority);
	if (!job) {
		ret = EOUTOFMEM;
		goto exit_function;
	}
	job->jobId = m->lastJobId;
	job->requestTime = steady_clock::now();
	m->persistentJob = job;

	/* Notify a waiting thread */
	m->condition.notify_one();

	/* wait until long job has been picked up */
	while (m->persistentJob)
		m->start_and_shutdown.wait(lck);
	m->lastJobId++;

exit_function:
	return ret;
}

int ThreadPool::addJob(
	start_routine func, void *arg, free_routine free_func, ThreadPriority prio)
{
	ThreadPoolJob *job{nullptr};

	std::unique_lock<std::mutex> lck(m->mutex);

	int totalJobs = m->highJobQ.size() + m->lowJobQ.size() + m->medJobQ.size();
	if (totalJobs >= m->attr.maxJobsTotal) {
		fprintf(stderr, "total jobs = %d, too many jobs\n", totalJobs);
		goto exit_function;
	}

	job = new ThreadPoolJob(func, arg, free_func, prio);
	if (!job)
		goto exit_function;
	job->jobId =  m->lastJobId;
	job->requestTime = steady_clock::now();
	switch (job->priority) {
	case HIGH_PRIORITY:
		m->highJobQ.push_back(job);
		break;
	case MED_PRIORITY:
		m->medJobQ.push_back(job);
		break;
	default:
		m->lowJobQ.push_back(job);
	}
	/* AddWorker if appropriate */
	m->addWorker(lck);
	/* Notify a waiting thread */
	m->condition.notify_one();
	m->lastJobId++;

exit_function:
	return 0;
}

int ThreadPool::getAttr(ThreadPoolAttr *out)
{
	if (!out)
		return EINVAL;
	if (!m->shuttingdown)
		m->mutex.lock();
	*out = m->attr;
	if (!m->shuttingdown)
		m->mutex.unlock();

	return 0;
}

int ThreadPool::setAttr(ThreadPoolAttr *attr)
{
	int retCode = 0;
	ThreadPoolAttr temp;
	int i = 0;

	std::unique_lock<std::mutex> lck(m->mutex);

	if (attr)
		temp = *attr;
	if (SetPolicyType(temp.schedPolicy) != 0) {
		return INVALID_POLICY;
	}
	m->attr = temp;
	/* add threads */
	if (m->totalThreads < m->attr.minThreads) {
		for (i = m->totalThreads; i < m->attr.minThreads; i++) {
			retCode = m->createWorker(lck);
			if (retCode != 0) {
				break;
			}
		}
	}
	/* signal changes */
	m->condition.notify_all();
	lck.unlock();

	if (retCode != 0)
		/* clean up if the min threads could not be created */
		m->shutdown();

	return retCode;
}

int ThreadPool::shutdown()
{
	if (m)
		return m->shutdown();
	return -1;
}

int ThreadPool::Internal::shutdown()
{
	ThreadPoolJob *temp = NULL;

	std::unique_lock<std::mutex> lck(mutex);

	while (this->highJobQ.size()) {
		temp = this->highJobQ.front();
		this->highJobQ.pop_front();
		delete temp;
	}

	while (this->medJobQ.size()) {
		temp = this->medJobQ.front();
		this->medJobQ.pop_front();
		delete temp;
	}

	while (this->lowJobQ.size()) {
		temp = this->lowJobQ.front();
		this->lowJobQ.pop_front();
		delete temp;
	}
	
	/* clean up long term job */
	if (this->persistentJob) {
		temp = this->persistentJob;
		delete temp;
		this->persistentJob = NULL;
	}
	/* signal shutdown */
	this->shuttingdown = 1;
	this->condition.notify_all();
	/* wait for all threads to finish */
	while (this->totalThreads > 0) {
		this->start_and_shutdown.wait(lck);
	}

	return 0;
}

void ThreadPoolPrintStats(ThreadPoolStats *stats)
{
	if (!stats)
		return;
	/* some OSses time_t length may depending on platform, promote it to long for safety */
	printf("ThreadPoolStats at Time: %ld\n", (long)time(nullptr));
	printf("High Jobs pending: %d\n", stats->currentJobsHQ);
	printf("Med Jobs Pending: %d\n", stats->currentJobsMQ);
	printf("Low Jobs Pending: %d\n", stats->currentJobsLQ);
	printf("Average Wait in High Priority Q in milliseconds: %f\n", stats->avgWaitHQ);
	printf("Average Wait in Med Priority Q in milliseconds: %f\n", stats->avgWaitMQ);
	printf("Averate Wait in Low Priority Q in milliseconds: %f\n", stats->avgWaitLQ);
	printf("Max Threads Active: %d\n", stats->maxThreads);
	printf("Current Worker Threads: %d\n", stats->workerThreads);
	printf("Current Persistent Threads: %d\n", stats->persistentThreads);
	printf("Current Idle Threads: %d\n", stats->idleThreads);
	printf("Total Threads : %d\n", stats->totalThreads);
	printf("Total Time spent Working in seconds: %f\n", stats->totalWorkTime);
	printf("Total Time spent Idle in seconds : %f\n", stats->totalIdleTime);
}

int ThreadPool::getStats(ThreadPoolStats *stats)
{
	if (nullptr == stats)
		return EINVAL;
	/* if not shutdown then acquire mutex */
	std::unique_lock<std::mutex> lck(m->mutex, std::defer_lock);
	if (!m->shuttingdown)
		lck.lock();

	*stats = m->stats;
	if (stats->totalJobsHQ > 0)
		stats->avgWaitHQ = stats->totalTimeHQ / (double)stats->totalJobsHQ;
	else
		stats->avgWaitHQ = 0.0;
	if (stats->totalJobsMQ > 0)
		stats->avgWaitMQ = stats->totalTimeMQ / (double)stats->totalJobsMQ;
	else
		stats->avgWaitMQ = 0.0;
	if (stats->totalJobsLQ > 0)
		stats->avgWaitLQ = stats->totalTimeLQ / (double)stats->totalJobsLQ;
	else
		stats->avgWaitLQ = 0.0;
	stats->totalThreads = m->totalThreads;
	stats->persistentThreads = m->persistentThreads;
	stats->currentJobsHQ = (int)m->highJobQ.size();
	stats->currentJobsLQ = (int)m->lowJobQ.size();
	stats->currentJobsMQ = (int)m->medJobQ.size();

	return 0;
}