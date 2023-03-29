/********************************************************************************
* @author: Cuyu Tang
* @email: me@expoli.tech
* @website: www.expoli.tech
* @date: 2023/3/29 9:40
* @version: 1.0
* @description: 
********************************************************************************/


#ifndef MYTINYWEBSERVER_TIMER_H
#define MYTINYWEBSERVER_TIMER_H

#include <netinet/in.h>

class UtilTimer;

/**
 * 客户端数据信息结构体
 */
struct client_data{
    sockaddr_in address;
    int socket_fd;
    UtilTimer *timer;
};

/**
 * 时间工具类
 */
class UtilTimer {
public:
    UtilTimer() : prev(nullptr),next(nullptr) {}
public:
    time_t expire{};
    void (*cb_func)(client_data *){};
    client_data *client_data{};
    UtilTimer *prev;
    UtilTimer *next;
};

class SortTimerList{
public:
    SortTimerList():head(nullptr),tail(nullptr){}
    ~SortTimerList(){
        UtilTimer *temp = head;
        while (temp){
            head = temp->next;
            delete temp;
            temp = head;
        }
    }
    bool add_timer(UtilTimer *timer);
    bool adjust_timer(UtilTimer *timer);
    bool del_timer(UtilTimer *timer);
    void tick();

private:
    UtilTimer *head;
    UtilTimer *tail;

private:
    void add_timer(UtilTimer *timer, UtilTimer *list_head);
};

#endif //MYTINYWEBSERVER_TIMER_H
