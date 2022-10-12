#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <semaphore.h>
#include <exception>

namespace lu {

/* class mutex */
class locker {
public:
    /* init mutex locker */
    locker() {
        if(pthread_mutex_init(&_mutex, NULL) != 0) {
            throw std::exception(); 
        }
    }
    /* destroy mutex locker */
    ~locker() {
        pthread_mutex_destroy(&_mutex);
    }
    /* lock mutex */
    bool lock() {
        return pthread_mutex_lock(&_mutex);
    }
    /* unlock mutex */
    bool unlock() {
        return pthread_mutex_unlock(&_mutex);
    }
private:
    pthread_mutex_t _mutex;
};

/* semaphore */
class sem {
public:
    /* init semaphore */
    sem() {
        if(sem_init(&_sem, 0, 0) != 0) {
            throw std::exception();
        }
    }
    sem(unsigned int val) {
        if(sem_init(&_sem, 0, val) != 0) {
            throw std::exception();
        }
    }
    /* destroy semaphore */
    ~sem() {
        sem_destroy(&_sem);
    }
    /* semaphore substract one */
    bool wait() {
        return sem_wait(&_sem) == 0;
    }
    /* semaphore add one */
    bool post() {
        return sem_post(&_sem) == 0;
    }
    /* get semaphore value */
    int getValue() {
        int sval;
        if(sem_getvalue(&_sem, &sval) != 0) {
            throw std::exception();
        }
        return sval;
    }
private:
    sem_t _sem;
};

/* condition variable */
class cond{
public:
    /* init condition variable & mutex */
    cond() {
        if(pthread_mutex_init(&_mutex, NULL) != 0) {
            throw std::exception(); 
        }
        /* if something is wrong, we should release mutex resource */
        if(pthread_cond_init(&_cond, NULL) != 0) {
            pthread_mutex_destroy(&_mutex);
            throw std::exception();
        }
    }
    /* destroy condition variable & mutex */
    ~cond() {
        pthread_cond_destroy(&_cond);
        pthread_mutex_destroy(&_mutex);
    }
    /* wait condition variable:
     *      first we should get the mutex locker
     */
    bool wait() {
        int ret = 0;
        pthread_mutex_lock(&_mutex);
        ret = pthread_cond_wait(&_cond, &_mutex);
        pthread_mutex_unlock(&_mutex);
        return ret == 0;
    }
    /* wakeup one thread of waiting for condition variable */
    bool signal() {
        return pthread_cond_signal(&_cond) == 0;
    }
    /* wakeup all thread of waiting for condition variable */
    bool broadcast() {
        return pthread_cond_broadcast(&_cond) == 0;
    }
private:
    pthread_cond_t _cond;
    pthread_mutex_t _mutex;
};

}

#endif