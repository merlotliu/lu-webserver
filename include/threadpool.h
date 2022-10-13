#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <list>
#include <cstdio>
#include <pthread.h>
#include <exception>

#include "locker.h"

//#define __DEBUG

#define THREAD_NUM_DEFAULT 8 /* default thread number */
#define MAX_TASKS_DEFAULT 10000 /* default max tasks */

namespace lu {

template<typename T>
class threadpool {
public:
    threadpool(int thread_number = THREAD_NUM_DEFAULT, 
        int max_tasks = MAX_TASKS_DEFAULT);
    ~threadpool();
    bool append(T *task);

private:
    static void *working(void *arg);
    void run();

private:
    int _thread_number; /* thread number */
    int _max_tasks; /* max length of tasks */
    pthread_t *_threads; /* threads array */
    std::list<T *> _task_queue /* task queue */;
    locker _queue_locker; /* mutex of task queue */
    sem _queue_stat; /* task number */
    bool _stop; /* is or not stop thread */
};

template<typename T>
threadpool<T>::threadpool(int thread_number, int max_tasks) 
    : _thread_number(thread_number), 
    _max_tasks(max_tasks), 
    _threads(NULL),
    _stop(false)  {
    if(thread_number <= 0 || max_tasks <= 0) {
        throw std::exception();   
    }
    // create threads
    _threads = new pthread_t[_thread_number];
    if(_threads == NULL) {
        throw std::exception();
    }
    for(int i = 0; i < _thread_number; i++) {
#ifdef __DEBUG
        printf( "create the %dth thread\n", i);
#endif
        if(pthread_create(&_threads[i], NULL, working, this) != 0) {
            delete [] _threads;
            throw std::exception();
        }
        // set thread detach
        if(pthread_detach(_threads[i]) != 0) {
            delete [] _threads;
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool() {
    delete [] _threads;
    _stop = true;
}

/* push task into task queue */
template<typename T>
bool threadpool<T>::append(T *task) {
    _queue_locker.lock();
    if(_task_queue.size() > _max_tasks) {
        _queue_locker.unlock();
        return false;    
    }
    _task_queue.push_back(task);
    _queue_locker.unlock();
    _queue_stat.post();
    return true;
}

template<typename T>
void *threadpool<T>::working(void *arg) {
    threadpool *pool = static_cast<threadpool *>(arg);
    pool->run();
    return pool;
}

/* keep getting task from task queue for working thread */
template<typename T>
void threadpool<T>::run() {
    while(!_stop) {
        _queue_stat.wait();
        _queue_locker.lock();
        if(_task_queue.empty()) {
            _queue_locker.unlock();
            continue;
        }
        T* task = _task_queue.front();
        _task_queue.pop_front();
        _queue_locker.unlock();
        if(task != NULL) {
            task->process();
        }
    }
}

}

#endif