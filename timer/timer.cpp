/********************************************************************************
* @author: Cuyu Tang
* @email: me@expoli.tech
* @website: www.expoli.tech
* @date: 2023/3/29 9:40
* @version: 1.0
* @description: 
********************************************************************************/


#include "timer.h"
#include "../log/log.h"

/**
 * 添加定时器
 * todo 添加定时器这个部分应该是有 bug，不确定，再看看
 * @param timer 要添加的定时器
 * @return
 */
bool SortTimerList::add_timer(UtilTimer *timer) {
    if (!timer) {
        return false;
    }
    if (!head) {
        head = tail = timer;
        return true;
    }
    /**
     * 如果插入的定时器的时间最小，就把它放在队头
     */
    if (timer->expire < head->expire) {
        timer->next = head;
        head->prev = timer;
        head = timer;
        return true;
    }
    /**
     * 调用私有方法，两个参数的重载版本
     */
    add_timer(timer, head);
    return true;
}

// 给我写出这一块的代码是什么意思
bool SortTimerList::adjust_timer(UtilTimer *timer) {
    if (!timer) {
        return false;
    }
    /**
     * 判断一个定时器下一个结点是否是空
     * 或者顺序错误
     */
    UtilTimer *temp = timer->next;
    if (!temp || (timer->expire < temp->expire)) {
        return false;
    }
    /**
     * 当定时器与头结点一样的时候，把头结点摘掉
     * 重新进行插入
     */
    if (timer == head) {
        head = head->next;
        head->prev = nullptr;
        timer->next = nullptr;
        add_timer(timer, head);
        return true;
    } else {
        /**
         * 将该结点拿出来，然后重新进行插入操作
         */
        timer->prev->next = timer->next;
        timer->next->prev = timer->prev;
        add_timer(timer, timer->next);
        return true;
    }
}

/**
 * 删除定时器
 * @param timer 要删除的定时器
 * @return
 */
bool SortTimerList::del_timer(UtilTimer *timer) {
    if (!timer)
        return false;
    /**
     * 当前队列中只有一个结点。
     */
    if ((timer == head) && (timer == tail)) {
        delete timer;
        head = nullptr;
        tail = nullptr;
        return true;
    }
    if (timer == head) {
        head = head->next;
        head->prev = nullptr;
        delete timer;
        return true;
    }
    if (timer == tail) {
        tail = tail->prev;
        tail->next = nullptr;
        delete timer;
        return true;
    }
    timer->prev->next = timer->next;
    timer->next->prev = timer->prev;
    delete timer;
    return true;
}

/**
 * 定时触发函数，同时检查是否有定时器超时
 * 如果有那就调用对应的回调函数，并删除对应的定时器
 */
void SortTimerList::tick() {
    if (!head)
        return;
    /**
     * 输出日志，并直接刷新缓冲区
     */
    LOG_INFO("%s", "timer tick");
    Log::get_instance()->flush();

    time_t cur = time(nullptr);
    UtilTimer *tmp = head;
    while (tmp) {
        // 还未到定时器的超时时间
        if (cur < tmp->expire) {
            break;
        }
        // 达到了超时时间，调用回调函数
        tmp->cb_func(tmp->client_data);
        // 更新定时器队列数据
        head = tmp->next;
        if (head) {
            head->prev = nullptr;
        }
        delete tmp;
        tmp = head;
    }
}

/**
 * 向有序队列中添加定时器
 * @param timer 要添加的定时器
 * @param list_head 队列头
 */
void SortTimerList::add_timer(UtilTimer *timer, UtilTimer *list_head) {
    UtilTimer *prev = list_head;
    UtilTimer *temp = prev->next;

    while (temp) {
        // 找到了插入位置
        if (timer->expire < temp->expire) {
            prev->next = timer;
            timer->prev = prev;
            timer->next = temp;
            temp->prev = timer;
            return;
        }
        prev = temp;
        temp = temp->next;
    }
    // 插入位置在末尾
    prev->next = timer;
    timer->prev = prev;
    timer->next = nullptr;
    tail = timer;
}