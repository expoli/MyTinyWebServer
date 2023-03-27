//
// Created by Cuyu Tang on 2023/3/27.
//

#include "ThreadPool.h"

/**
 * 使用 '= default' 定义一个简单的默认构造函数
 * @tparam T
 */
template<typename T>
ThreadPool<T>::ThreadPool() = default;

/**
 *
 * @tparam T
 * @param thread_number
 * @param max_request
 */
template<typename T>
ThreadPool<T>::ThreadPool(int thread_number, int max_request) : m_thread_number(thread_number), m_max_request(max_request),m_stop(false), m_threads(nullptr) {
    // 判断参数是否有误
    if (thread_number <= 0 || max_request <= 0)
        throw std::exception();
    // 新建线程池的数组，其大小为 m_thread_number
    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();
    // 循环创建线程，并将工作线程按要求进行运行
    for (int i = 0; i < thread_number; ++i) {
        //printf("create the %dth thread\n",i);
        // 如果创建新线程失败，进行相关的错误处理
        if (pthread_create(m_threads+i, NULL,worker, this) != 0){
            delete[] m_threads;
            throw std::exception();
        }
        /**
         * 将线程进行分离后，不用单独对工作线程进行回收
         * 指示线程 TH 永远不会与 PTHREAD_JOIN 连接。
         * 因此，TH 的资源将在它终止时立即被释放，而不是等待另一个线程对其执行 PTHREAD_JOIN。
         */
        if (pthread_detach(m_threads[i])){
            delete [] m_threads;
            throw std::exception();
        }
    }
}

/**
 * 析构函数、回收已经分配的资源，并把线程的停止标志设置为 true
 * @tparam T
 */
template<typename T>
ThreadPool<T>::~ThreadPool() {
    delete [] m_threads;
    m_stop = true;
}

/**
 * 首先将传入的参数arg转换为线程池的指针，然后调用线程池的run函数。
 * 内部访问私有成员函数 run ，完成线程处理要求。
 * 最后，将线程池的指针作为返回值返回。
 * 这个函数的作用是让线程执行线程池中的任务。
 * @tparam T
 * @param arg
 * @return
 */
template<typename T>
void *ThreadPool<T>::worker(void *arg) {
    auto *pool = (ThreadPool *)arg;
    pool->run();
    return pool;
}

/**
 *
 * @tparam T
 */
template<typename T>
void ThreadPool<T>::run() {
    // 在线程未停止的情况下不断在队列中取出数据进行执行
    while (!m_stop){
        // 等待任务需要处理的信号量
        m_queue_stat.wait();
        // 有任务需要进行处理，被唤醒后对队列进行加锁
        m_queue_locker.lock();
        // 如果请求队列为空，解锁，跳过后续操作
        if (m_work_queue.empty()){
            m_queue_locker.unlock();
            continue;
        }
        // 获取请求队列中的第一个请求
        T *request = m_work_queue.front();
        // 将这个请求从请求队列中去除
        m_work_queue.pop_front();
        // 去除对请求队列的加锁，给其它线程进行处理的机会
        m_queue_locker.unlock();
        // 请求为空，不进行处理
        if (!request)
            continue;
        // 执行请求处理函数
        request->process();
    }
}

template<typename T>
bool ThreadPool<T>::append(T *request) {
    // 对请求队列进行加锁
    m_queue_locker.lock();
    // 判断此时队列是否已经满，队列已满，添加失败
    if (m_work_queue.size() >= m_max_request){
        m_queue_locker.unlock();
        return false;
    }
    // 将新请求添加到队列中
    m_work_queue.push_back(request);
    // 对请求队列进行解锁
    m_queue_locker.unlock();
    // 以原子操作方式将队列信号量加一
    m_queue_stat.post();
    // 全部完成
    return true;
}

