/********************************************************************************
* @author: Cuyu Tang
* @email: me@expoli.tech
* @website: www.expoli.tech
* @date: 2023/3/28 10:52
* @version: 1.0
* @description: 
********************************************************************************/


#include <cstring>
#include <cstdarg>
#include "log.h"

Log::Log() {
    m_count = 0;
    m_is_async = false;
}

/**
 * 析构函数，处理文件指针
 */
Log::~Log() {
    if (m_fp!= nullptr){
        fclose(m_fp);
    }
    // 释放日志缓存
    delete[] m_buf;
}

/**
 * 初始化函数
 * todo 判断文件夹是否存在的函数，并给出默认的位置（比如运行目录下的 log 文件夹）
 * @param file_name log 文件名字
 * @param log_buf_size log 日志缓存区大小
 * @param split_lines 最大行数
 * @param max_queue_size 最长日志条队列
 * @return
 */
bool Log::init(const char *file_name, int log_buf_size, int split_lines, int max_queue_size) {
    // 如果设置了 max_queue_size,则设置为异步
    if (max_queue_size >= 1){
        m_is_async = true;
        m_log_queue = new BlockQueue<std::string>(max_queue_size);

        pthread_t tid;
        /**
         * flush_log_thread为回调函数,这里表示创建线程异步写日志
         */
        pthread_create(&tid, nullptr,flush_log_thread, nullptr);
    }
    /**
     * 初始化日志缓存区
     */
    m_log_buf_size = log_buf_size;
    m_buf = new char [m_log_buf_size];
    memset(m_buf, '\0',m_log_buf_size);
    m_split_lines = split_lines;
    /**
     * 记录初始化时间
     */
    time_t t = time(nullptr);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    /**
     * 找到字符串中最后一次出现的字符
     * 返回指向 C 字符串 str 中字符最后一次出现的指针。
     * 终止空字符被认为是 C 字符串的一部分。
     * 因此，它也可以定位到检索指向字符串末尾的指针。
     * a/c/d/aaa.log
     */
    const char *p = strrchr(file_name,'/');
    // 保存日志全部的名称
    char log_full_name[256] = {0};
    /**
     * 年_月_日_filename
     */
    if (p == nullptr){
        snprintf(log_full_name,255,"%d_%02d_%02d_%s",my_tm.tm_year+1900,my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    } else {
        // 日志文件名字
        strcpy(m_log_name, p + 1);
        // 将文件名除最后的日志名之外的所有字符串当作文件夹名字
        strncpy(m_dir_name,file_name,p-file_name+1);
        snprintf(log_full_name, 255, "%s/%d_%02d_%02d_%s", m_dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, m_log_name);
    }

    m_today = my_tm.tm_mday;
    // 以追加的方式打开日志文件
    m_fp = fopen(log_full_name,"a");
    if (m_fp == nullptr)
        return false;
    return true;
}

/**
 * 日志写函数
 * @param level 日志等级
 * @param format 日志的格式
 * @param ...
 */
void Log::write_log(int level, const char *format, ...) {
    struct timeval now = {0, 0};
    gettimeofday(&now, nullptr);

    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    char s[16] = {0};

    switch (level)
    {
        case 0:
            strcpy(s, "[debug]:");
            break;
        case 1:
            strcpy(s, "[info]:");
            break;
        case 2:
            strcpy(s, "[warn]:");
            break;
        case 3:
            strcpy(s, "[erro]:");
            break;
        default:
            strcpy(s, "[info]:");
            break;
    }
    /**
     * 写入一个log，对m_count++, m_split_lines最大行数
     */
    m_mutex.lock();
    m_count++;
    /**
     * 对日志按日期进行分类
     */
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0){
        char new_log[256] = {0};
        /*
         * 刷新缓存区内容，并关闭原来的文件描述符
         */
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};

        snprintf(tail, 15, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        if (m_today != my_tm.tm_mday){
            snprintf(new_log, 255, "%s/%s%s", m_dir_name, tail, m_log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        } else{
            snprintf(new_log, 255, "%s/%s%s.%lld", m_dir_name, tail, m_log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }

    m_mutex.unlock();

    // 如果此调用来自用户程序，则为用户定义标准宏。
    va_list args;
    va_start(args, format);

    std::string log_str;
    m_mutex.lock();
    /**
     * 2000-01-01 00:00:00.123456 [debug]
     */
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
                     my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    // PrintFError ("Error opening '%s'",szFileName);
    int m = vsnprintf(m_buf + n, m_log_buf_size - 1, format, args);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();
    /**
     * 异步写入、但是队列未满，将 log 字符串压入队列
     */
    if (m_is_async && !m_log_queue->full()){
        m_log_queue->push(log_str);
    } else{
        m_mutex.lock();
        fputs(log_str.c_str(),m_fp);
        m_mutex.unlock();
    }
    va_end(args);
}

void Log::flush()
{
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}