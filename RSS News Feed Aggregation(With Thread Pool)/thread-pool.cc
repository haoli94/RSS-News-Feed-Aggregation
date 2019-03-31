/**
 * File: thread-pool.cc
 * --------------------
 * Presents the implementation of the ThreadPool class.
 */

#include "thread-pool.h"
#include <thread> 
#include <mutex>
#include <queue>
#include "semaphore.h"
#include <condition_variable>


using namespace std;



ThreadPool::ThreadPool(size_t numThreads) : wts(numThreads),thunkQueue(numThreads),exit(false) {
	dt = thread([this](){dispatcher();});
	totalNumThreads = numThreads;
	for (size_t workerID = 0; workerID < numThreads; workerID++) {
		unique_ptr<semaphore>& semaph = thunkQueue[workerID].first;
		if (semaph == nullptr){
		    semaph.reset(new semaphore(0));
		}
		wts[workerID] = thread([this](size_t workerID) {worker(workerID);
				}, workerID);
		threadQueue.push(workerID);
	}
}


void ThreadPool::dispatcher(){
	while (true) {
		jobLock.lock();
		jobCV.wait(jobLock,[this] {return !jobQueue.empty() || exit;});
		if (exit) // threadpool done
		{
			jobLock.unlock();
			return;
		}
		const function<void(void)> thunk = jobQueue.front();
		jobQueue.pop();
		jobCV.notify_all();
		jobLock.unlock();

		threadLock.lock();
		threadCV.wait(threadLock, [this]{return !threadQueue.empty();});
		size_t availableWorkerID = threadQueue.front();
		threadQueue.pop();
		threadLock.unlock();
		thunkQueue[availableWorkerID].second = thunk;
		thunkQueue[availableWorkerID].first->signal();
	}
}

void ThreadPool::worker(size_t workerID){
	while (true) {
		thunkQueue[workerID].first->wait();
		lock_guard<mutex> lg(sLock);
		if (exit) return;
		thunkQueue[workerID].second();
		threadQueue.push(workerID);
		threadCV.notify_all();
	}
}

void ThreadPool::schedule(const function<void(void)>& thunk) {
	jobLock.lock();
	jobQueue.push(thunk);
	jobLock.unlock();
	jobCV.notify_all();	

}
void ThreadPool::wait() {
	jobLock.lock();
	jobCV.wait(jobLock, [this]{return jobQueue.empty();});
	jobLock.unlock();	
	threadLock.lock();
	threadCV.wait(threadLock, [this]{return totalNumThreads == threadQueue.size();});
	threadLock.unlock();
}
ThreadPool::~ThreadPool() {
	sLock.lock();
	wait();
	exit = true;
	jobCV.notify_all();
	sLock.unlock();
	threadCV.notify_all();
	dt.join();
	for (std::pair<std::unique_ptr<semaphore>, std::function<void(void)>> &thunk : thunkQueue){
		thunk.first->signal();
		thunk.first.release();
	}
	for (thread &t : wts) t.join();
}
