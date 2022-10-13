#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h> 
#include <sys/epoll.h> 
#include <cstdio>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unordered_map>
#include <stdarg.h>

#include "tools.h"

//#define __DEBUG /* debug flag */

namespace lu{

class http_conn{
    
public:
    static const int READ_BUFFER_SIZE = 2048; /* read buffer size */
    static const int WRITE_BUFFER_SIZE = 1024; /* write buffer size */
    static const int FILENAME_LEN = 256; /* file name max length */
    static const int WRITE_IOVCNT_MAX = 2; /* max number of buffers */

    static const char *DOC_ROOT; /* resource root path */

    /* http code info */
    static std::unordered_map<int, const char *> RESPONSE_CODE_TITLE; /* response code title */
    static std::unordered_map<int, const char *> RESPONSE_CODE_FORM; /* reponse code content */

public:
    /* main state machine state : parse http by 3 parts */
    enum CHECK_STATE { 
        CHECK_STATE_REQUESTLINE = 0, /* parse request head line */
        CHECK_STATE_HEADER, /* parse request header other part except head line */
        CHECK_STATE_CONTENT /* parse request content */
    };
    /* submachine state(main state internal call) : get a full line */ 
    enum LINE_STATUS { 
        LINE_OK = 0, /* fully line */
        LINE_BAD, /* error line */
        LINE_OPEN /* line is not completed */
    };
    /* http request parse code */
    enum HTTP_CODE { 
        NO_REQUEST, /* request is not completed, continue to read */
        GET_REQUEST, /* fully client request */
        FILE_REQUEST = 200, /* file request */
        BAD_REQUEST = 400, /* syntax error in request */
        FORBIDDEN_REQUEST = 403, /* no access */
        NO_RESOURCE = 404, /* no request resource */
        INTERNAL_ERROR = 500, /* server internal error */
        CLOSED_CONNECTION /* client close disconnection */
    };
    /* requst method, only support GET */
    enum METHOD {
        GET = 0,
        POST, 
        HEAD, 
        PUT, 
        DELETE, 
        TRACE, 
        OPTIONS, 
        CONNECT
    };

public:
    http_conn();
    ~http_conn();

public:
    /* init http connction */
    void init(int connfd, const sockaddr_in &client_addr);
    /* nonblocking read */
    bool read();
    /* parse http request & make reponse */
    void process();
    /* nonblocking write */
    bool write();
    /* close connction */
    void close();

private:
    /* init internal data */
    void _init();

    /* parse http request every line */
    HTTP_CODE process_read();
    /* parse one line according '\r\n' & replace '\r\n' to '\0\0' */
    LINE_STATUS parse_line();
    /* parse request line to get request method, request url, HTTP version */
    HTTP_CODE parse_request_line(char * text);
    /* parse headers to get key-value */
    HTTP_CODE parse_headers(char * text);
    /* parse request content */
    HTTP_CODE parse_content(char * text);
    /* according parse result to find resource in server & waiting for write to client */
    HTTP_CODE do_request();
    
    /* create response content according result code of parse http request */
    bool process_write(HTTP_CODE code);
    /* response line */
    bool add_status_line(int status, const char *title);
    /* response headers */
    bool add_headers(int content_len);
    /* response content */
    bool add_content(const char *content);
    /* response headers : Content-Length */
    bool add_content_length(int content_len);
    /* response headers : Content-Type */
    bool add_content_type();
    /* response headers : Connection */
    bool add_linger();
    /* '\r\n' */
    bool add_blank_line();
    /* write data to write buffer write for sending */
    bool add_reponse(const char* format, ... );
private:
    /* get current line head address */
    inline char *get_line() { return _read_buf + _start_line; }
    /* unmap */
    inline bool unmap() {
        int ret = false;
        if(_file_address != NULL) {
            ret = munmap(_file_address, _file_stat.st_size);
            _file_address = NULL;
        }
        return ret;
    }

public:
    static int _epollfd; /* epoll fd */
    static int _user_count; /* connctions count */

private:
    int _connfd; /* cur http connction fd  */
    sockaddr_in _client_addr; /* client address */

    /* read about */
    int _read_idx; /* current pos in read buffer */
    char _read_buf[READ_BUFFER_SIZE]; /* read buffer */

    /* write about */
    int _write_idx; /* current pos in write buffer */
    char _write_buf[WRITE_BUFFER_SIZE]; /* write buffer */
    int _iovcnt; /* Number of buffers */
    struct iovec _iov[WRITE_IOVCNT_MAX]; /* iovec write buffers array */
    int _bytes_to_send; /* current need to send numbers of bytes */
    int _bytes_already_send; /* current already send numbers of bytes */

    /* http parse about */
    CHECK_STATE _check_state; /* main state machine state */
    int _checked_idx; /* current parse char position in read buffer */
    int _start_line; /* current line start index relative to read buffer head address */
    
    char *_url; /* request url */
    METHOD _method; /* request method */
    char *_version; /* http protocol version */

    int _content_length; /* request content length */
    bool _linger; /* is or not keep alive */
    char *_host; /* host address with point & number */

    char _real_file[FILENAME_LEN]; /* request file path in server */
    struct stat _file_stat; /* file status */
    char *_file_address; /* mmap memory map address */
};

}
#endif