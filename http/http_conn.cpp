/********************************************************************************
* @author: Cuyu Tang
* @email: me@expoli.tech
* @website: www.expoli.tech
* @date: 2023/3/27 17:02
* @version: 1.0
* @description: 
********************************************************************************/


#include <fcntl.h>
#include <sys/epoll.h>
#include <csignal>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <sys/mman.h>
#include <cstdarg>
#include <sys/uio.h>
#include "http_conn.h"
#include "../config/config.h"
#include "../log/log.h"

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

/**
 * 对文件描述符设置非阻塞
 * @param fd 需要设置的文件描述符
 * @return 原来的文件描述符配置选项
 */
int set_nonblocking(int fd) {
    // 获取原来的文件描述符配置项
    int old_option = fcntl(fd, F_GETFL);
    // 在老配置上加上非阻塞配置
    int new_option = old_option | O_NONBLOCK;
    // 用新配置配置对应的文件描述符
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/**
 * 向内核事件表中注册读事件，ET 模式（边缘触发模式），可选择开启 EPOLLONESHOT
 * @param epoll_fd epoll 文件描述符
 * @param fd 要配置的文件描述符
 * @param one_shot EPOLLONESHOT 配置
 */
void add_fd(int epoll_fd, int fd, bool one_shot) {
    // 声明 epoll 事件变量
    epoll_event event{};
    // 将要配置的描述符，加入到事件里面的数据部分
    event.data.fd = fd;

#ifdef conn_fdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef conn_fdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif

#ifdef listen_fdET
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
#endif

#ifdef listen_fdLT
    event.events = EPOLLIN | EPOLLRDHUP;
#endif
    // 如果配置 one_shot 再配置上 one shot 配置
    if (one_shot)
        event.events |= EPOLLONESHOT;
    // 添加 epoll 事件描述符
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    // 设置非阻塞
    set_nonblocking(fd);
}

/**
 * 从 epoll 内核事件表中，删除对应文件描述符注册
 * @param epoll_fd epoll 内核事件表文件描述符
 * @param fd 要删除的文件描述符
 */
void remove_fd(int epoll_fd, int fd) {
    // 调用 epoll_ctl api 从内核时间表中，删除对应文件描述符
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    // 关闭对应的文件描述符
    close(fd);
}

/**
 * 将事件重置为 EPOLLONESHOT
 * @param epoll_fd epoll 内核事件表文件描述符
 * @param fd 要修改配置的文件描述符
 * @param ev 其它的自定义 events 配置
 */
void mod_fd(int epoll_fd, int fd, int ev) {
    epoll_event event{};
    event.data.fd = fd;

#ifdef conn_fdET
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
#endif

#ifdef conn_fdLT
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP;
#endif

    // 调用 epoll_ctl 修改对应描述符的 epoll 模式
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
}

// 初始化用户数静态变量
int http_conn::m_user_count = 0;
// 初始化 epoll 内核事件表静态文件描述符变量为 -1
int http_conn::m_epoll_fd = -1;

/**
 * 关闭连接，关闭一个连接，客户总量减一
 * @param real_close 是否是真的关闭连接
 */
void http_conn::close_conn(bool real_close) {
    if (real_close && m_socket_fd != -1) {
        remove_fd(m_epoll_fd, m_socket_fd);
        m_socket_fd = -1;
        m_user_count--;
    }
}

/**
 * 初始化连接,由外部调用来初始化套接字地址
 * @param socket_fd 要初始化的 socket 文件描述符 id
 * @param addr 客户端地址信息
 */
void http_conn::init(int socket_fd, const sockaddr_in &addr) {
    m_socket_fd = socket_fd;
    m_address = addr;

    // 将对应的 socket 文件描述符，添加到内核 epoll 事件表中
    add_fd(m_epoll_fd, socket_fd, true);
    m_user_count++;
    init();
    LOG_DEBUG("%s now have %d users!", "init connection done!", m_user_count);
}

/**
 * 初始化新接受的连接
 * check_state 默认为分析请求行状态
 */
void http_conn::init() {
    bytes_to_send = 0;
    bytes_have_send = 0;
    m_check_state = CHECK_STATE_REQUEST_LINE;

    m_keepalive = false;
    m_method = GET;
    m_url = nullptr;
    m_version = nullptr;
    m_host = nullptr;
    m_content_length = 0;

    m_line_start_idx = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;

    cgi = 0;

    // 初始化所有的 buff 缓存区为 0
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buff, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN);
}

http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    LOG_DEBUG("%s", "parse_line start!");
    // 在还未监测完缓存区中的所有字符时，依次对缓存区内容
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        // 解析到某一行的末尾
        if (temp == '\r') {
            // \r 后面就是这一行缓存的末尾，即达到了读取的 buff 缓存末尾
            if ((m_checked_idx + 1) == m_read_idx)
                return LINE_OPEN;
                // \r 后面就是 \n 即将这一行、完全读取完毕
            else if (m_read_buf[m_checked_idx + 1] == '\n') {
                // 在 buff 里面添加 null 字符，形成字符串
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            // 其它情况就是这一行读取出现问题
            return LINE_BAD;
        } else if (temp == '\n') {
            // 读取到 \n ，如果前面是 \r 就代表这一行已经读取完毕了
            if (m_checked_idx > 1 && m_read_buf[m_checked_idx - 1] == '\r') {
                // 在 buff 里面添加 null 字符，形成字符串
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    // 这行 buff 未读取到末尾，返回开放状态，继续进行操作
    return LINE_OPEN;
}

/**
 * 循环读取客户数据，直到无数据可读或对方关闭连接
 * 非阻塞ET工作模式下，需要一次性将数据读完
 * @return
 */
bool http_conn::read_once() {
    // 如果缓存区已经满了，直接返回失败
    if (m_read_idx >= READ_BUFFER_SIZE)
        return false;
    // 读取多少字节
    long bytes_read;

/**
 * 水平触发
 */
#ifdef conn_fdLT
    // 从客户端 socket 连接里读取数据
    bytes_read = recv(m_socket_fd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
    // 调用 recv 失败，进行错误处理
    if (bytes_read <= 0) {
        return false;
    }

    m_read_idx += bytes_read;
    return true;
#endif

/**
 * ET 模式，即边缘触发模式需要一次性全部读完
 */
#ifdef conn_fdET
    while (true) {
        // 从客户端 socket 连接里读取数据
        bytes_read = recv(m_socket_fd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        // 错误处理
        if (bytes_read == -1) {
            // todo 这些错误信息需要详细进行处理
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return false;
        } else if (bytes_read == 0) {
            return false;
        }
        m_read_idx += bytes_read;
    }
    return true;
#endif
}

/**
 * todo 这个 url 请求行解析细节还没摸清除
 * @param text 请求内容
 * @return 请求行解析状态码
 */
http_conn::HTTP_CODE http_conn::parse_request_line(char *text) {
    /**
     * 查找 ACCEPT 中任何字符在 S 中的第一次出现。即查找空格或者 \t
     *
     * GET /home.html HTTP/1.1
     *    *
     */
    m_url = strpbrk(text, " \t");
    if (!m_url) {
        return REQUEST_BAD;
    }
    /**
     * 请求头第一个空格或者 \t 出现的时候，前面就是请求方式
     *
     * GET /home.html HTTP/1.1
     *    0
     *     *(m_url)
     */
    *m_url++ = '\0';
    // 上一步添加 \0 之后，就可以成为一个字符串，即 method
    char *method = text;
    if (strcasecmp(method, "GET") == 0)
        m_method = GET;
    else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
        cgi = 1;
    } else
        return REQUEST_BAD;
    /**
     * 获取字符串中字符集的跨度
     * 返回 str1 的初始部分的长度，该部分仅包含属于 str2 的字符。
     * 搜索不包括任一字符串的终止空字符，但到此为止。
     *
     * 即查找 str2 后面的，第一次出现的位置。
     * 即找到 url 开始的地方
     *
     * GET /home.html HTTP/1.1
     *     *(m_url)
     */
    m_url += strspn(m_url, " \t");

    /**
     * 返回一个指针，该指针指向作为 str2 一部分的任何字符在 str1 中的第一次出现，
     * 如果没有匹配项，则返回一个空指针。
     * 搜索不包括任一字符串的终止空字符，但到此为止。
     *
     * 即滑动到 http 版本部分的字符串部分。
     * GET /home.html HTTP/1.1
     *               *(m_version)
     */
    m_version = strpbrk(m_url, " \t");

    if (!m_version)
        return REQUEST_BAD;
    /**
     * 加上 null 字符封装成字符串，即将 m_url 封装成字符串
     * GET /home.html HTTP/1.1
     *               \0
     */
    *m_version++ = '\0';
    /**
     * 不进行改变了
     */
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
        return REQUEST_BAD;
    /**
     * 兼容 http(s):// 开头的 url ，并重置为 /
     */
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    } else if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        m_url = strchr(m_url, '/');
    }
    /**
     * url 格式错误，进行错误处理
     */
    if (!m_url || m_url[0] != '/')
        return REQUEST_BAD;
    // todo 当 url 为 / 时，显示判断界面，应该是直接跳转 index.html，需要修改
    if (strlen(m_url) == 1)
        strcat(m_url, "index.html");
    /**
     * 请求行判断完毕，下面需要进行请求头的判断
     */
    m_check_state = CHECK_STATE_HEADER;
    // 没有问题，url 解析正常
    return REQUEST_OK;
}

/**
 * 解析请求头
 * @param text 要解析的内容
 * @return 解析状态码
 */
http_conn::HTTP_CODE http_conn::parse_headers(char *text) {
    /**
     * 请求头解析完毕
     * todo post 与 get 请求并没有区分，需要修改
     */
    if (text[0] == '\0') {
        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return REQUEST_NO;
        }
        m_check_state = CHECK_STATE_CONTENT;
        return REQUEST_GET;
    } // 解析请求头中的 Connection: 行
    else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        /**
         * 跳过冒号后面的空格
         * Connection: keep-alive
         */
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
            m_keepalive = true;
    } // 解析 Content-length:
    else if (strncasecmp(text, "Content-length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        char *pEnd;
        m_content_length = strtol(text, &pEnd, 10);
    } // 解析 Host:
    else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } // 错误处理
    else {
        printf("oop!unknow header: %s\n", text);
        //LOG_INFO("oop!unknow header: %s", text);
    }
    return REQUEST_NO;
}

/**
 * 判断http请求是否被完整读入
 * @param text 输入的请求
 * @return 解析的代码
 */
http_conn::HTTP_CODE http_conn::parse_content(char *text) {
    // 读取的 id 数目，达到了内容长度与已经检查过的字符数，即已经完全读取完毕
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        // 添加字符串末尾标志
        text[m_content_length] = '\0';
        // POST 请求中的数据内容
        m_string = text;
        return REQUEST_GET;
    }
    return REQUEST_NO;
}

http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = REQUEST_NO;
    char *text;

    while ((m_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK) ||
           ((line_status = parse_line()) == LINE_OK)) {
        // 获取这一行开始处理的指针
        text = get_line();
        // 更新下一行开始处理的 id
        m_line_start_idx = m_checked_idx;
        // 输出日志
        LOG_INFO("%s", text);
        // 刷新缓冲区
        Log::get_instance()->flush();
        switch (m_check_state) {
            // 主状态机：检查请求行
            case CHECK_STATE_REQUEST_LINE: {
                ret = parse_request_line(text);
                if (ret == REQUEST_BAD)
                    return REQUEST_BAD;
                break;
            } // 状态机：检查请求头
            case CHECK_STATE_HEADER: {
                ret = parse_headers(text);
                break;
            }
            // 状态机：POST 检查内容
            /**
             * todo 这种处理方式还不太合理，后面还需要进行优化处理
             * 可以考虑增加状态机状态， 或者使用 m_method 进行分类处理
             */
            case CHECK_STATE_CONTENT: {
                LOG_DEBUG("%s", "CHECK_STATE_CONTENT");
                if (m_method == GET){
                    return do_request();
                } else if (m_method == POST){
                    ret = parse_content(text);
                    if (ret == REQUEST_GET)
                        return do_request();
                    line_status = LINE_OPEN;
                    break;
                }
            }
            default:
                LOG_ERROR("%s", "process_read error! INTERNAL_ERROR");
                return INTERNAL_ERROR;
        }
    }
    return ret;
}

/**
 * todo 原来的 cgi 是根据 url 请求的 id 来进行 cgi 行为判断，这种处理方式感觉不对劲
 * @return
 */
http_conn::HTTP_CODE http_conn::do_request() {
    strcpy(m_real_file, WWW_ROOT_DIR);
    size_t len = strlen(WWW_ROOT_DIR);

    //printf("m_url:%s\n", m_url);
    const char *p = strstr(m_url, "/");
    // 只有 /
    if (strlen(p) == 1) {
        strcat(m_real_file, "/index.html");
    } else
        // todo 需要检查这个路径拼接合并是否合理
        strcat(m_real_file, p);

    /**
     * 检查文件状态
     */
    // 1. 文件是否存在
    if (stat(m_real_file, &m_file_stat) < 0)
        return REQUEST_NO_RESOURCE;
    // 2. 文件是否有读取权限
    if (!(m_file_stat.st_mode & S_IROTH))
        return REQUEST_FORBIDDEN;
    // 3. 文件是否是文件夹
    if (S_ISDIR(m_file_stat.st_mode))
        return REQUEST_BAD;
    // 以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    /**
     * 映射地址从 ADDR 附近开始并扩展 LEN 字节。
     * 从OFFSET根据PROT和FLAGS进入FD描述的文件。
     * 如果 ADDR 不为零，则它是所需的映射地址。
     * 如果在 FLAGS 中设置了 MAP_FIXED 位，则映射将恰好位于 ADDR（必须是页面对齐的）；
     * 否则系统会选择一个方便的附近地址。
     *
     * 返回值是实际选择的映射地址或错误的 MAP_FAILED（在这种情况下设置了“errno”）。
     * 成功的“mmap”调用会释放受影响区域的所有先前映射。
     */
    m_file_address = (char *) mmap(nullptr, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    // 文件映射出错
    if (!m_file_address) {
        close(fd);
        LOG_ERROR("%s", errno);
        return INTERNAL_ERROR;
    }
    close(fd);
    return REQUEST_FILE;
}

/**
 * 取消文件映射
 */
void http_conn::unmap() {
    if (m_file_address) {
        int ret = munmap(m_file_address, m_file_stat.st_size);
        if (ret == 0)
            m_file_address = nullptr;
        else {
            LOG_ERROR("%s", errno);
        }
    }
}

bool http_conn::add_response(const char *format, ...) {
    // 缓存用完了
    if (m_write_idx >= WRITE_BUFFER_SIZE)
        return false;
    va_list args;
    va_start(args, format);

    int len = vsnprintf(m_write_buff + m_write_idx, WRITE_BUFFER_SIZE - 1, format, args);
    // 如果写入的数据超过缓冲区中还剩余的空间，则引发错误
    if (len >= WRITE_BUFFER_SIZE - 1 - m_write_idx) {
        va_end(args);
        return false;
    }
    // 更新 id
    m_write_idx += len;
    va_end(args);
    // todo 这是直接把缓冲区中的所有内容，都显示出来了？不应该是显示从 后面显示的数据嘛
    LOG_INFO("%s", m_write_buff);
    // 刷新缓冲区
    Log::get_instance()->flush();
    return true;
}

/**
 * 添加状态行
 * @param status
 * @param title
 * @return
 */
bool http_conn::add_status_line(int status, const char *title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

/**
 * 添加响应头
 * @param content_len 内容长度
 * @return 是否成功
 */
bool http_conn::add_headers(size_t content_len) {
    bool r1 = add_content_length(content_len);
    bool r2 = add_linger();
    bool r3 = add_blank_line();
    return r1 && r2 && r3;
}

/**
 * 添加内容长度
 * @param content_len
 * @return
 */
bool http_conn::add_content_length(size_t content_len) {
    return add_response("Content-Length:%d\r\n", content_len);
}

/**
 * 响应文件类型
 * @return
 */
bool http_conn::add_content_type() {
    return add_response("Content-Type:%s\r\n", "text/html");
}

/**
 * 添加是否是长连接
 * @return
 */
bool http_conn::add_linger() {
    return add_response("Connection:%s\r\n", m_keepalive ? "keep-alive" : "close");
}

/**
 * 添加空行
 * @return
 */
bool http_conn::add_blank_line() {
    return add_response("%s", "\r\n");
}

/**
 * 添加内容信息
 * todo 直接以字符形式放入，会不会出现问题？没有对文件类型进行处理
 * @param content 内容
 * @return
 */
bool http_conn::add_content(const char *content) {
    return add_response("%s", content);
}

/**
 * 写处理函数
 * @param ret 根据状态码进行响应的处理
 * @return
 */
bool http_conn::process_write(HTTP_CODE ret) {
    switch (ret) {
        case INTERNAL_ERROR: {
            add_status_line(500, error_500_title);
            add_headers(strlen(error_500_form));
            if (!add_content(error_500_form))
                return false;
            break;
        }
        case REQUEST_NO_RESOURCE: {
            add_status_line(404, error_404_title);
            add_headers(strlen(error_404_form));
            if (!add_content(error_404_form))
                return false;
            break;
        }
        case REQUEST_FORBIDDEN: {
            add_status_line(403, error_403_title);
            add_headers(strlen(error_403_form));
            if (!add_content(error_403_form))
                return false;
            break;
        }
        case REQUEST_FILE: {
            add_status_line(200, ok_200_title);
            if (m_file_stat.st_size != 0) {
                add_headers(m_file_stat.st_size);
                m_iv[0].iov_base = m_write_buff;
                m_iv[0].iov_len = m_write_idx;
                m_iv[1].iov_base = m_file_address;
                m_iv[1].iov_len = m_file_stat.st_size;
                m_iv_count = 2;
                bytes_to_send = m_write_idx + m_file_stat.st_size;
                return true;
            } else {
                const char *ok_string = "<html><body></body></html>";
                add_headers(strlen(ok_string));
                if (!add_content(ok_string))
                    return false;
            }
        }
        default:
            return false;
    }
    m_iv[0].iov_base = m_write_buff;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}

/**
 * 处理请求
 */
void http_conn::process() {
    HTTP_CODE read_ret = process_read();
    if (read_ret == REQUEST_NO || read_ret == REQUEST_OK) {
        mod_fd(m_epoll_fd, m_socket_fd, EPOLLIN);
        return;
    }
    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }
    mod_fd(m_epoll_fd, m_socket_fd, EPOLLOUT);
}

bool http_conn::write() {
    long n = 0;
    /**
     * 连接建立，但要发送的数据为0，重新向内核事件表中注册 one shout 事件
     */
    if (bytes_to_send == 0) {
        mod_fd(m_epoll_fd, m_socket_fd, EPOLLIN);
        init();
        return true;
    }

    while (true) {
        n = writev(m_socket_fd, m_iv, m_iv_count);
        if (n < 0) {
            if (errno == EAGAIN) {
                mod_fd(m_epoll_fd, m_socket_fd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_have_send += n;
        bytes_to_send -= n;
        /**
         * 发送完毕
         * todo 要进行深度理解
         */
        if (bytes_have_send >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address + (bytes_have_send - m_write_idx);
            m_iv[1].iov_len = bytes_to_send;
        } else {
            m_iv[0].iov_base = m_write_buff + bytes_have_send;
            m_iv[0].iov_len = m_iv[0].iov_len - bytes_have_send;
        }

        if (bytes_to_send <= 0) {
            unmap();
            mod_fd(m_epoll_fd, m_socket_fd, EPOLLIN);
            if (m_keepalive) {
                init();
                return true;
            } else {
                return false;
            }
        }
    }
}