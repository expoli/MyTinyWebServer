/********************************************************************************
* @author: Cuyu Tang
* @email: me@expoli.tech
* @website: www.expoli.tech
* @date: 2023/3/27 17:02
* @version: 1.0
* @description: 
********************************************************************************/


#ifndef MYTINYWEBSERVER_HTTP_CONN_H
#define MYTINYWEBSERVER_HTTP_CONN_H


#include <netinet/in.h>
#include <sys/stat.h>

class http_conn {
public:
    static const int FILENAME_LEN = 200;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 1024;

    enum METHOD {
        GET = 0,
        POST,
        HEAD,
        PUT,
        DELETE,
        TRACE,
        OPTIONS,
        CONNECT,
        PATH
    };
    enum HTTP_CODE {
        REQUEST_OK,
        REQUEST_NO,
        REQUEST_GET,
        REQUEST_BAD,
        REQUEST_NO_RESOURCE,
        REQUEST_FORBIDDEN,
        REQUEST_FILE,
        INTERNAL_ERROR,
        CLOSED_CONNECTION
    };
    enum CHECK_STATE {
        CHECK_STATE_REQUEST_LINE = 0,
        CHECK_STATE_HEADER,
        CHECK_STATE_CONTENT
    };
    enum LINE_STATUS {
        LINE_OK = 0,
        LINE_BAD,
        LINE_OPEN
    };

public:
    static int m_epoll_fd;
    static int m_user_count;

public:
    http_conn() = default;

    ~http_conn() = default;

public:
    void init(int socket_fd, const sockaddr_in &addr);

    void close_conn(bool real_close = true);

    void process();

    bool read_once();

    bool write();

    sockaddr_in *get_address() {
        return &m_address;
    }

private:
    int m_socket_fd{};        // 代表此连接的 socket 文件描述符
    sockaddr_in m_address{};  // 客户端连接地址
    char m_read_buf[READ_BUFFER_SIZE]{};  // 读取缓存区
    long m_read_idx{};     // 开始读取的字节游标
    int m_checked_idx{};  // 已经通过检查的字节游标
    int m_line_start_idx{};   // 开始处理的行

    char m_write_buff[WRITE_BUFFER_SIZE]{};   // 写缓存区
    int m_write_idx{};    // 写游标

    CHECK_STATE m_check_state;  // 从状态机标志
    METHOD m_method;    // HTTP 请求方法
    char m_real_file[FILENAME_LEN]{};  // 真实文件名
    char *m_url{};    // 请求路径
    char *m_version{};    // HTTP 请求版本
    char *m_host{};   // http 请求头中的 host 字段
    long m_content_length{};   // 内容长度
    bool m_keepalive{};   // 是否开启长连接
    char *m_file_address{};   //

    struct stat m_file_stat{};    // 文件权限状态
    struct iovec m_iv[2]{};   //
    int m_iv_count{};
    int cgi{};    // 是否启用的POST
    char *m_string{}; // 存储请求头数据

    long bytes_to_send{};  // 要发送的数据
    long bytes_have_send{};    // 已经发送的数据

private:
    void init();

    HTTP_CODE process_read();

    bool process_write(HTTP_CODE ret);

    HTTP_CODE parse_request_line(char *text);

    HTTP_CODE parse_headers(char *text);

    HTTP_CODE parse_content(char *text);

    HTTP_CODE do_request();

    char *get_line() { return m_read_buf + m_line_start_idx; };

    LINE_STATUS parse_line();

    void unmap();

    bool add_response(const char *format, ...);

    bool add_content(const char *content);

    bool add_status_line(int status, const char *title);

    bool add_headers(size_t content_length);

    bool add_content_type();

    bool add_content_length(size_t content_length);

    bool add_linger();

    bool add_blank_line();
};


#endif //MYTINYWEBSERVER_HTTP_CONN_H
