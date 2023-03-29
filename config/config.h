/********************************************************************************
* @author: Cuyu Tang
* @email: me@expoli.tech
* @website: www.expoli.tech
* @date: 2023/3/27 19:09
* @version: 1.0
* @description: 
********************************************************************************/


#ifndef MYTINYWEBSERVER_CONFIG_H
#define MYTINYWEBSERVER_CONFIG_H

static const char *WWW_ROOT_DIR = "www";

// 日志参数配置
static const int BLOCK_QUEUE_SIZE = 1000;
static const int LOG_BUFF_SIZE = 8192;
static const int LOG_SPLIT_LINES = 5000000;
static const int LOG_MAX_QUEUE_SIZE = 0;   // 默认为0，即同步日志模式

#define MAX_FD 65536           //最大文件描述符
#define MAX_EVENT_NUMBER 10000 //最大事件数
#define TIMESLOT 5             //最小超时单位

#define THREAD_NUMBER 8     // 线程池的大小
#define MAX_REQUEST 10000   // 线程池中的请求队列的长度

#define SYNC_LOG  //同步写日志
//#define ASYNC_LOG //异步写日志

#define conn_fdET //边缘触发非阻塞
//#define conn_fdLT //水平触发阻塞

#define listen_fdET //边缘触发非阻塞
//#define listen_fdLT //水平触发阻塞

#endif //MYTINYWEBSERVER_CONFIG_H
