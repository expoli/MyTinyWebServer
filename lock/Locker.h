//
// Created by Cuyu Tang on 2023/3/27.
//

#ifndef MYTINYWEBSERVER_LOCKER_H
#define MYTINYWEBSERVER_LOCKER_H

#include <pthread.h>
#include <exception>
#include <semaphore.h>

/**
 * 锁的类封装，内部使用线程互斥锁实现
 */
class Locker {
    pthread_mutex_t m_mutex{};
public:
    // pthread_mutex_init函数用于初始化互斥锁
    Locker()
    {
        if (pthread_mutex_init(&m_mutex, nullptr) != 0)
            throw std::exception();
    }

    // pthread_mutex_destory函数用于销毁互斥锁
    ~Locker() { pthread_mutex_destroy(&m_mutex); }

    // pthread_mutex_lock函数以原子操作方式给互斥锁加锁
    bool lock() { return pthread_mutex_lock(&m_mutex); }

    // pthread_mutex_unlock函数以原子操作方式给互斥锁解锁
    bool unlock() { return pthread_mutex_unlock(&m_mutex); }

    pthread_mutex_t *get() { return &m_mutex; }
};

/**
 * 条件变量
 * 条件变量提供了一种线程间的通知机制,当某个共享数据达到某个值时,唤醒等待这个共享数据的线程.
 */
class Cond {
    //static pthread_mutex_t m_mutex;
    pthread_cond_t m_cond{};

public:
    Cond()
    {
        // pthread_cond_init函数用于初始化条件变量
        if (pthread_cond_init(&m_cond, nullptr) != 0)
            throw std::exception();
    }

    // pthread_cond_destory函数销毁条件变量
    ~Cond() { pthread_cond_destroy(&m_cond); }

    /**
     * pthread_cond_wait函数用于等待目标条件变量.
     * 该函数调用时需要传入 mutex参数(加锁的互斥锁) ,
     * 函数执行时,先把调用线程放入条件变量的请求队列,
     * 然后将互斥锁mutex解锁,当函数成功返回为0时,互斥锁会再次被锁上.
     * 也就是说函数内部会有一次解锁和加锁操作.
     * @param m_mutex
     * @return
     */
    bool wait(pthread_mutex_t *m_mutex) { return pthread_cond_wait(&m_cond,m_mutex) == 0; }

    bool time_wait(pthread_mutex_t *m_mutex, struct timespec t) { return pthread_cond_timedwait(&m_cond,m_mutex,&t) == 0; }

    bool signal() { return pthread_cond_signal(&m_cond) == 0; }

    // pthread_cond_broadcast函数以广播的方式唤醒所有等待目标条件变量的线程
    bool broadcast() { return pthread_cond_broadcast(&m_cond) == 0; }
};

/**
 * 信号量锁机制
 */
class Sem {
    // 信号量
    sem_t m_sem{};
public:
    Sem()
    {
        // sem_init函数用于初始化一个未命名的信号量
        if (sem_init(&m_sem,0,0) != 0)
            throw std::exception();
    }

    // 单参数构造函数必须标记为显式以避免无意的隐式转换
    explicit Sem(int num)
    {
        if (sem_init(&m_sem,0,num) != 0)
            throw std::exception();
    }

    // sem_destory函数用于销毁信号量
    ~Sem() { sem_destroy(&m_sem); }

    // sem_wait函数将以原子操作方式将信号量减一,信号量为0时,sem_wait阻塞
    bool wait() { return sem_wait(&m_sem) == 0; }

    // sem_post函数以原子操作方式将信号量加一,信号量大于0时,唤醒调用sem_post的线程
    bool post() { return sem_post(&m_sem) ==0; }
};
#endif //MYTINYWEBSERVER_LOCKER_H
