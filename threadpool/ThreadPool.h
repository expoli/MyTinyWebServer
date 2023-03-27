//
// Created by Cuyu Tang on 2023/3/27.
//

#ifndef MYTINYWEBSERVER_THREADPOOL_H
#define MYTINYWEBSERVER_THREADPOOL_H

#include <pthread.h>
#include <list>
#include "../lock/Locker.h"

/**
 * 线程池类
 * @tparam T
 */
template <typename T>
class ThreadPool {
private:
    int m_thread_number{};          // 线程池中现在的线程数
    int m_max_request{};            // 请求队列中允许的最大请求数
    pthread_t *m_threads{};         // 描述线程池的数组，其大小为 m_thread_number
    std::list<T *> m_work_queue;    // 请求队列
    Locker m_queue_locker;          // 保护请求队列的互斥锁
    Sem m_queue_stat;               // 描述是否有任务需要处理的信号量
    bool m_stop{};                  // 是否结束线程的标志

private:
    /**
     * 工作线程运行的函数，不断从工作队列中取出任务并执行
     * @param arg 传递给工作线程的参数
     * @return 返回工作线程的地址
     */
    static void *worker(void *arg);
    void run();

public:
    ThreadPool();
    /**
     *
     * @param thread_number 默认线程池构造函数中的线程个数
     * @param max_request 等待处理的请求最大的数量
     */
    explicit ThreadPool(int thread_number = 8, int max_request = 10000);
    ~ThreadPool();

    // 将请求队列中插入任务请求
    bool append(T *request);
};

#endif //MYTINYWEBSERVER_THREADPOOL_H
