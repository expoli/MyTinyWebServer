#include <sys/socket.h>
#include <cstdio>
#include <cstring>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <fcntl.h>
#include <cstdlib>
#include <cassert>
#include <csignal>

#include "config/config.h"
#include "timer/timer.h"
#include "log/log.h"
#include "lock/Locker.h"
#include "threadpool/ThreadPool.h"
#include "http/http_conn.h"


//这三个函数在http_conn.cpp中定义，改变链接属性
extern int add_fd(int epoll_fd, int fd, bool one_shot);

extern int remove(int epoll_fd, int fd);

extern int set_nonblocking(int fd);

//设置定时器相关参数
static int pipe_line_fd[2];
static SortTimerList timer_list;
static int epoll_fd = 0;

//信号处理函数
void sig_handler(int sig);

//添加信号函数
void add_sig(int sig, void(handler)(int), bool restart = true);

//定时处理任务，重新定时以不断触发SIGALRM信号
void timer_handler();

//定时器回调函数，删除非活动连接在socket上的注册事件，并关闭
void cb_func(ClientData *client_data);

// 错误显示函数
void show_error(int conn_fd, const char *info);

int main(int argc, char *argv[]) {
#ifdef ASYNC_LOG
    Log::get_instance()->init("ServerLog",LOG_BUFF_SIZE,LOG_SPLIT_LINES,8);
#endif
#ifdef SYNC_LOG
    Log::get_instance()->init("ServerLog", LOG_BUFF_SIZE, LOG_SPLIT_LINES, 0);
#endif
    if (argc <= 1) {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    char *end;
    /**
     * 第二个才是端口，原代码写错了，同时没有对指定 ip 进行处理
     * todo 可以考虑提一个 pr
     */
    long port = strtol(argv[2], &end, 10);
    LOG_INFO("listen port: %ld\n", port);
    Log::get_instance()->flush();
    if (errno) {
        printf("%d", errno);
        throw std::exception();
    }
    /**
     * todo 这个函数的作用
     */
    add_sig(SIGPIPE, SIG_IGN);

    ThreadPool<http_conn> *thread_pool;
    try {
        thread_pool = new ThreadPool<http_conn>(THREAD_NUMBER, MAX_REQUEST);
    } catch (...) {
        throw std::exception();
    }
    /**
     * 创建客户端 http 连接池
     */
    auto *clients = new http_conn[MAX_FD];
    assert(clients);
    /**
     * 创建 socket 文件描述符，流类型
     */
    int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listen_fd >= 0);

    long ret;
    sockaddr_in address{};
    bzero(&address, sizeof address);
    address.sin_family = AF_INET;
    // todo 命令读取了地址，但是没有使用，可以进行改进
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    int flag = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof flag);
    ret = bind(listen_fd, (struct sockaddr *) &address, sizeof address);
    assert(ret >= 0);
    /**
     * todo 开始监听，第二个应该是缓存队列还是什么的需要进一步确认
     */
    ret = listen(listen_fd, 5);
    assert(ret >= 0);

    /**
     * 创建内核事件表
     */
    epoll_event events[MAX_EVENT_NUMBER];
    epoll_fd = epoll_create(5);
    assert(epoll_fd != -1);

    add_fd(epoll_fd, listen_fd, false);
    /**
     * 设置静态变量，内核 epoll 内核事件表描述符
     */
    http_conn::m_epoll_fd = epoll_fd;

    /**
     * 创建管道
     */
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, pipe_line_fd);
    assert(ret != -1);
    /**
     * 设置管道表现
     * todo 判断哪个是读端，哪个是写端
     */
    set_nonblocking(pipe_line_fd[1]);
    add_fd(epoll_fd, pipe_line_fd[0], false);

    /**
     * 添加定时器信号
     */
    add_sig(SIGALRM, sig_handler, false);
    /**
     * 添加终止信号
     */
    add_sig(SIGTERM, sig_handler, false);
    bool stop_server = false;

    /**
     * 客户端对应的定时器
     */
    auto *clients_timer = new ClientData[MAX_FD];
    bool timeout = false;
    /**
     * 以配置的描述，调度定时器，默认超时时间为 5s
     */
    alarm(TIMESLOT);

    LOG_INFO("%s", "Server running...");
    Log::get_instance()->flush();
    while (!stop_server) {
        /**
         * 获取 epoll 事件触发数目
         * timeout 为 -1，表示一直等待，直到有事件发生
         */
        int number = epoll_wait(epoll_fd, events, MAX_EVENT_NUMBER, -1);
        /**
         * todo EINTR 错误信息是什么，代表什么
         */
        if (number < 0 && errno != EINTR) {
            printf("%s", "epoll runtime failure!");
            LOG_ERROR("%s","epoll runtime failure!");
            break;
        }

        /**
         * 循环处理所有的事件
         */
        for (int i = 0; i < number; ++i) {
            int socket_fd = events[i].data.fd;
            /**
             * 即服务端的监听文件描述符，监听到了事件活动
             */
            if (socket_fd == listen_fd) {
                sockaddr_in client_address{};
                socklen_t client_addr_len = sizeof client_address;

#ifdef listen_fdLT
                /**
                 * 水平触发处理方式
                 */
                int conn_fd = accept(listen_fd,(struct sockaddr*)&client_address,&client_addr_len);
                if (conn_fd < 0){
                    LOG_ERROR("%s:errno is:%d", "accept error", errno);
                    continue;
                }
                /**
                 * 服务器忙处理
                 */
                if (http_conn::m_user_count >= MAX_FD){
                    show_error(conn_fd, "Internal server busy");
                    LOG_ERROR("%s", "Internal server busy");
                    continue;
                }
                /**
                 * 初始化客户端连接
                 */
                clients[conn_fd].init(conn_fd,client_address);

                clients_timer[conn_fd].address = client_address;
                clients_timer[conn_fd].socket_fd = conn_fd;
                UtilTimer *timer = new UtilTimer;
                timer->client_data = &clients_timer[conn_fd];
                timer->cb_func = cb_func;

                time_t cur = time(nullptr);
                timer->expire = cur + 3 * TIMESLOT;
                clients_timer[conn_fd].timer = timer;
                timer_list.add_timer(timer);
#endif

#ifdef listen_fdET
                /**
                 * 边缘触发模式，一直接收连接并处理
                 */
                while (true) {
                    int conn_fd = accept(listen_fd, (struct sockaddr *) &client_address, &client_addr_len);
                    if (conn_fd < 0) {
                        LOG_ERROR("%s:errno is:%d", "accept error", errno);
                        break;
                    }
                    if (http_conn::m_user_count >= MAX_FD) {
                        show_error(conn_fd, "Internal server busy");
                        LOG_ERROR("%s", "Internal server busy");
                        break;
                    }
                    clients[conn_fd].init(conn_fd, client_address);

                    clients_timer[conn_fd].address = client_address;
                    clients_timer[conn_fd].socket_fd = conn_fd;

                    auto *timer = new UtilTimer;
                    timer->client_data = &clients_timer[conn_fd];
                    timer->cb_func = cb_func;

                    time_t cur = time(nullptr);
                    timer->expire = cur + 3 * TIMESLOT;
                    clients_timer[conn_fd].timer = timer;
                    timer_list.add_timer(timer);
                }
                continue;
#endif
            }
                /**
                 * 服务器端关闭连接情况处理
                 */
            else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                UtilTimer *timer = clients_timer[socket_fd].timer;
                timer->cb_func(&clients_timer[socket_fd]);

                if (timer) {
                    timer_list.del_timer(timer);
                }
            }
                /**
                 * 管道信号
                 */
            else if ((socket_fd == pipe_line_fd[0]) && (events[i].events & EPOLLIN)) {
                long sig;
                char signals[1024];
                ret = recv(pipe_line_fd[0], signals, sizeof signals, 0);
                if (ret == -1) {
                    continue;
                } else if (ret == 0) {
                    continue;
                } else {
                    for (int j = 0; j < ret; ++j) {
                        switch (signals[i]) {
                            case SIGALRM: {
                                timeout = true;
                                break;
                            }
                            case SIGTERM: {
                                stop_server = true;
                            }
                        }
                    }
                }
            }
                /**
                 * 读事件
                 * 处理客户连接上接收到的数据
                 */
            else if (events->events & EPOLLIN) {
                UtilTimer *timer = clients_timer[socket_fd].timer;
                if (clients[socket_fd].read_once()) {
                    LOG_INFO("deal with the client(%s)", inet_ntoa(clients[socket_fd].get_address()->sin_addr));
                    thread_pool->append(clients + socket_fd);

                    if (timer) {
                        time_t cur = time(nullptr);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_list.adjust_timer(timer);
                    }
                } else {
                    timer->cb_func(&clients_timer[socket_fd]);
                    if (timer) {
                        timer_list.del_timer(timer);
                    }
                }
            }
                /**
                 * 写事件
                 */
            else if (events[i].events & EPOLLOUT) {
                UtilTimer *timer = clients_timer[socket_fd].timer;
                if (clients[socket_fd].write()) {
                    LOG_INFO("send data to the client(%s)", inet_ntoa(clients[socket_fd].get_address()->sin_addr));
                    /**
                     * 若有数据传输，则将定时器往后延迟3个单位
                     * 并对新的定时器在链表上的位置进行调整
                     * 以符合链表的要求
                     */
                    if (timer) {
                        time_t cur = time(nullptr);
                        timer->expire = cur + 3 * TIMESLOT;
                        LOG_INFO("%s", "adjust timer once");
                        Log::get_instance()->flush();
                        timer_list.adjust_timer(timer);
                    }
                } else {
                    timer->cb_func(&clients_timer[socket_fd]);
                    if (timer) {
                        timer_list.del_timer(timer);
                    }
                }
            }
            if (timeout) {
                timer_handler();
                timeout = false;
            }
        }
    }
    /**
     * 关闭连接
     */
    close(epoll_fd);
    close(listen_fd);
    close(pipe_line_fd[0]);
    close(pipe_line_fd[1]);
    delete[] clients;
    delete[] clients_timer;
    delete thread_pool;
    return 0;
}

/**
 * 信号处理函数
 * @param sig 信号量
 */
void sig_handler(int sig) {
    /**
     * 保留原来的errno，在函数最后恢复，以保证函数的可重入性
     */
    int save_errno = errno;
    int msg = sig;
    /**
     * 将信号写入管道，以通知主循环
     * todo 向管道的写端写入信号量（这个地方只发送了一个字节够用吗？
     */
    send(pipe_line_fd[1], (char *) &msg, 1, 0);
    errno = save_errno;
}

/**
 * 设置信号处理函数
 * @param sig
 * @param handler
 * @param restart
 */
void add_sig(int sig, void(handler)(int), bool restart) {
    /**
     * 描述信号到达时要采取的操作的结构。
     */
    struct sigaction sa{};
    memset(&sa, '\0', sizeof(sa));
    /**
     * 信号量到达后需要执行的处理函数
     */
    sa.sa_handler = handler;
    /**
     * 在信号返回时重新启动系统调用。
     */
    if (restart) {
        sa.sa_flags |= SA_RESTART;
    }
    /**
     * sigfillset(&sa.sa_mask)是将所有信号加入到信号集中，表示在处理该信号时，阻塞所有其他信号。
     * 这个函数的作用是为了在信号处理函数中防止其他信号的干扰，保证信号处理函数的正确执行。
     */
    sigfillset(&sa.sa_mask);
    /**
     * 设置信号量动作
     */
    assert(sigaction(sig, &sa, nullptr) != -1);
}

/**
 * 定时处理任务，重新定时以不断触发SIGALRM信号
 */
void timer_handler() {
    timer_list.tick();
    /**
     * 因为一次alarm调用只会引起一次SIGALRM信号，所以我们要重新定时，以不断触发SIGALRM信号
     */
    alarm(TIMESLOT);
}

/**
 * 定时器回调函数
 * 删除非活动连接在socket上的注册事件，并关闭
 * @param client_data
 */
void cb_func(ClientData *client_data) {
    assert(client_data);
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_data->socket_fd, nullptr);
    close(client_data->socket_fd);
    http_conn::m_user_count--;
    LOG_INFO("close fd %d", client_data->socket_fd);
    Log::get_instance()->flush();
}

/**
 * 显示错误信息
 * @param conn_fd 要发送的socket
 * @param info 要发送的信息
 */
void show_error(int conn_fd, const char *info) {
    send(conn_fd, info, strlen(info), 0);
    close(conn_fd);
}