/********************************************************************************
* @author: Cuyu Tang
* @email: me@expoli.tech
* @website: www.expoli.tech
* @date: 2023/3/28 10:52
* @version: 1.0
* @description: 
********************************************************************************/


#ifndef MYTINYWEBSERVER_LOG_H
#define MYTINYWEBSERVER_LOG_H


#include <cstdio>
#include <string>
#include "block_queue.h"


class Log {
private:
    static const int LOG_FILE_NAME_LENGTH = 128;
    char m_dir_name[LOG_FILE_NAME_LENGTH];    //路径名
    char m_log_name[LOG_FILE_NAME_LENGTH];    //log文件名

    int m_split_lines;  //日志最大行数
    int m_log_buf_size; //日志缓冲区大小
    long long m_count;  //日志行数记录
    int m_today;        //因为按天分类,记录当前时间是那一天
    FILE *m_fp;         //打开log的文件指针
    char *m_buf;

    BlockQueue<std::string> *m_log_queue;   // 阻塞队列，内部是循环队列
    bool m_is_async;                        //是否同步标志位
    Locker m_mutex;

private:
    Log();
    virtual ~Log();
    /**
     * 异步写日志，内联函数
     */
    void * async_write_log(){
        std::string single_log;
        /**
         * 当队列不为空的时候，从队列中取出并写入文件描述符
         */
        while (m_log_queue->pop(single_log)){
            m_mutex.lock();
            fputs(single_log.c_str(),m_fp);
            m_mutex.unlock();
        }
    }
public:
    /**
     * C++11以后,使用局部变量懒汉不用加锁
     * @return 单例应用
     */
    static Log* get_instance(){
        static Log instance;
        return &instance;
    }

    /**
     * 异步写日志函数
     * @param args 函数参数
     * @return
     */
    static void * flush_log_thread(void *args){
        Log::get_instance()->async_write_log();
    }
    /**
     * 可选择的参数有日志文件、日志缓冲区大小、最大行数以及最长日志条队列
     * @param file_name 日志文件名
     * @param log_buf_size 日志缓冲区大小
     * @param split_lines 最大行数
     * @param max_queue_size 最长日志条队列
     * @return
     */
    bool init(const char *file_name, int log_buf_size = LOG_BUFF_SIZE, int split_lines = LOG_SPLIT_LINES, int max_queue_size = MAX_QUEUE_SIZE);

    void write_log(int level, const char *format, ...);

    void flush();

};

#define LOG_DEBUG(format, ...) Log::get_instance()->write_log(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Log::get_instance()->write_log(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) Log::get_instance()->write_log(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Log::get_instance()->write_log(3, format, ##__VA_ARGS__)

#endif //MYTINYWEBSERVER_LOG_H
