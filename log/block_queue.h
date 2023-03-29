/********************************************************************************
* @author: Cuyu Tang
* @email: me@expoli.tech
* @website: www.expoli.tech
* @date: 2023/3/28 10:58
* @version: 1.0
* @description: 
********************************************************************************/


#ifndef MYTINYWEBSERVER_BLOCK_QUEUE_H
#define MYTINYWEBSERVER_BLOCK_QUEUE_H

#include <cstdlib>
#include <sys/time.h>
#include "../lock/Locker.h"
#include "../config/config.h"

template<class T>
class BlockQueue {
    Locker m_mutex;
    Cond m_cond;

    T *m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;

public:
    BlockQueue() {
        m_max_size = BLOCK_QUEUE_SIZE;
        m_array = new T[m_max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    explicit BlockQueue(int max_size = BLOCK_QUEUE_SIZE) {
        if (max_size <= 0)
            exit(-1);

        m_max_size = max_size;
        m_array = new T[max_size];
        m_size = 0;
        m_front = -1;
        m_back = -1;
    }

    /**
     * 析构函数，清除构造函数中分配的内存
     */
    ~BlockQueue() {
        m_mutex.lock();
        if (m_array != nullptr)
            delete[] m_array;
        m_mutex.unlock();
    }

    /**
     * 清空队列
     */
    void clear() {
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }

    /**
     * 判断队列是否满了
     * @return 是否满了
     */
    bool full() {
        m_mutex.lock();
        if (m_size >= m_max_size) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    /**
     * 判断队列是否为空
     * @return
     */
    bool empty() {
        m_mutex.lock();
        if (m_size == 0) {
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }

    /**
     * 返回队首元素
     * @param value 获取地址
     * @return 是否获取成功
     */
    bool front(T &value) {
        m_mutex.lock();
        if (0 == m_size) {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }

    /**
     * 返回队尾元素
     * @param value 值存储的位置
     * @return 是否执行成功
     */
    bool back(T &value) {
        m_mutex.lock();
        if (0 == m_size) {
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }

    /**
     * 获取队列长度
     * @return
     */
    int size() {
        int temp = 0;
        m_mutex.lock();
        temp = m_size;
        m_mutex.unlock();
        return temp;
    }

    /**
     * 获取当前的队列最大长度
     * @return
     */
    int max_size() {
        int temp = 0;
        m_mutex.lock();
        temp = m_max_size;
        m_mutex.unlock();
        return temp;
    }

    /**
     * 往队列添加元素，需要将所有使用队列的线程先唤醒
     * 当有元素push进队列,相当于生产者生产了一个元素
     * 若当前没有线程等待条件变量,则唤醒无意义
     * @param item 要 push 进队列的数据
     * @return
     */
    bool push(const T &item) {
        m_mutex.lock();
        if (m_size >= m_max_size) {
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;

        m_size++;

        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }

    /**
     * pop 时,如果当前队列没有元素,将会等待条件变量
     * @param item 存储 pop 出的元素内容
     * @return
     */
    bool pop(T &item) {
        m_mutex.lock();
        /*
         * 队列为空的时候
         */
        while (m_size <= 0) {
            /*
             * 等待信号量有活动，如果队列不为空，则跳出循环
             */
            if (!m_cond.wait(m_mutex.get())) {
                m_mutex.unlock();
                return false;
            }
        }
        // 取出循环队列中的队头元素
        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }

    /**
     * 增加了超时处理的函数
     * @param item 保存数据的变量
     * @param ms_timeout 超时时间
     * @return 函数是否执行成功
     */
    bool pop(T &item, int ms_timeout) {
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};

        gettimeofday(&now, nullptr);
        m_mutex.lock();
        /**
         * 如果队列为空
         */
        if (m_size <= 0) {
            t.tv_sec = now.tv_sec + ms_timeout / 1000;  // 秒
            t.tv_nsec = (ms_timeout % 1000) * 1000;     // ms
            // 等待队列信号量的输入
            if (!m_cond.time_wait(m_mutex.get(), t)) {
                m_mutex.unlock();
                return false;
            }
        }
        // todo 这又做了一次校验、如果不再进行一次校验会怎样？
        if (m_size <= 0) {
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;
        m_mutex.unlock();
        return true;
    }
};

#endif //MYTINYWEBSERVER_BLOCK_QUEUE_H
