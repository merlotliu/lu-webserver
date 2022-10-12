#include "http_conn.h"

namespace lu {

/* init static */
int http_conn::_epollfd = -1;
int http_conn::_user_count = 0;

/* reource root path */
const char *http_conn::DOC_ROOT = "/home/merlotliu/lu-webserver/resources";

/* http code info */
std::unordered_map<int, const char *> http_conn::RESPONSE_CODE_TITLE = {
    {FILE_REQUEST, "OK"},
    {BAD_REQUEST, "Bad Request"},
    {FORBIDDEN_REQUEST, "Forbidden"},
    {NO_RESOURCE, "Not Found"},
    {INTERNAL_ERROR, "Internal Error"}
};
std::unordered_map<int, const char *> http_conn::RESPONSE_CODE_FORM = {
    {FILE_REQUEST, ""},
    {BAD_REQUEST, "Your request has bad syntax or is inherently impossible to satisfy.\n"},
    {FORBIDDEN_REQUEST, "You do not have permission to get file from this server.\n"},
    {NO_RESOURCE, "The requested file was not found on this server.\n"},
    {INTERNAL_ERROR, "There was an unusual problem serving the requested file.\n"}
};

/* nouse */
http_conn::http_conn() {}
http_conn::~http_conn() {}

/* initialize user connction */
void http_conn::init(int connfd, const sockaddr_in &addr) {
    _connfd = connfd;
    _client_addr = addr;
    
#ifdef __DEBUG
    //[1] for test
    int reuse = 1;
    setsockopt(connfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    //![1]
#endif

    tools::addfd(_epollfd, connfd, true);
    _user_count++;

    _init();
}

void http_conn::_init() {
    /* read about */
    _read_idx = 0; /* current pos in read buffer */
    bzero(_read_buf, READ_BUFFER_SIZE); /* read buffer clear */

    /* write about */
    _write_idx = 0; /* current pos in write buffer */
    bzero(_write_buf, WRITE_BUFFER_SIZE); /* write buffer clear */
    _iovcnt = 0; /* Number of buffers */
    _bytes_already_send = 0; /* current already send numbers of bytes */
    _bytes_to_send = 0; /* current need to send numbers of bytes */

    /* http parse about */
    _check_state = CHECK_STATE_REQUESTLINE; /* main state machine state */
    _checked_idx = 0; /* current parse char position in read buffer */
    _start_line = 0; /* current line start index relative to read buffer head address */
    
    _url = NULL; /* request url */
    _method = GET; /* default request GET */
    _version = NULL; /* http protocol version */

    _content_length = 0; /* request content length */
    _linger = false; /* is or not keep alive */
    _host = NULL; /* host address with point & number */

    bzero(_real_file, FILENAME_LEN); /* request file path in server */
    bzero(&_file_stat, sizeof(_file_stat)); /* file status */
    _file_address = NULL; /* mmap memory map address */
}

/* Reading client data util no data or client disconnct */
bool http_conn::read() {
    if(_read_idx >= READ_BUFFER_SIZE) { /* buffer is full */
        return false;
    }
    int bytes_read = 0;
    while(true) {
        /* start from last read index in buffer to read new data */
        bytes_read = recv(_connfd, _read_buf + _read_idx, 
            READ_BUFFER_SIZE - _read_idx, 0);
        if(bytes_read == -1) {
            if(errno == EAGAIN || errno == EWOULDBLOCK) { /* read the end of data */
                break;
            }
            return false;
        } else if(bytes_read == 0) { /* connection disable */
            return false;
        } else { /* read normal */
            _read_idx += bytes_read;
        }
    }

    return true;
}

/* Being executed by working thread. Main entry function of handle HTTP request */
void http_conn::process() {
    /* parse http request */
#ifdef __DEBUG
    printf("\nprocess...\n");
#endif
    HTTP_CODE read_ret = process_read();
    if(read_ret == NO_REQUEST) {
        tools::modifyfd(_epollfd, _connfd, EPOLLIN);
        return;
    }
    /* make response */
    bool write_ret = process_write(read_ret);
    if(!write_ret) {
        close();
    }
    tools::modifyfd(_epollfd, _connfd, EPOLLOUT);
#ifdef __DEBUG
    printf("\nepoll fd : %d, connection fd : %d\n", _epollfd, _connfd);
    printf("\nwrite buffer : \n%s\n", _write_buf);
#endif
}

/* write to */
bool http_conn::write() {
#ifdef __DEBUG
    printf("\nwrite...\n");
#endif
    if(_bytes_to_send <= 0) {
        tools::modifyfd(_epollfd, _connfd, EPOLLIN);
        _init();
        return true;
    }

    int cur_wbytes = 0;
    while(true) {
        cur_wbytes = writev(_connfd, _iov, _iovcnt);
        if(cur_wbytes <= -1) {
            if(errno == EAGAIN) {
                tools::modifyfd(_epollfd, _connfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        _bytes_already_send += cur_wbytes;
        _bytes_to_send -= cur_wbytes;

        if(_bytes_to_send <= 0) {
            unmap();
            tools::modifyfd(_epollfd, _connfd, EPOLLIN);
            _init();
            return _linger;
        }
        if(_bytes_already_send < _write_idx) { /* first buffer have data need to write */

            _iov[0].iov_base = _write_buf + _bytes_already_send;
            _iov[0].iov_len -= cur_wbytes;
        } else { /* first buffer done */
            _iov[0].iov_len = 0;
            _iov[1].iov_base = _file_address + (_bytes_already_send - _write_idx);
            _iov[1].iov_len -= _bytes_to_send;
        }
    }

    return true;
}

/* close connction */
void http_conn::close() {
    if(_connfd != -1) {
        tools::removefd(_epollfd, _connfd);
        _connfd = -1;
        http_conn::_user_count--;
    }
}

/* parse http request every line */
http_conn::HTTP_CODE http_conn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    /* parse request every line */
    while((_check_state == CHECK_STATE_CONTENT && line_status == LINE_OK)
        || ((line_status = parse_line()) == LINE_OK)) {
        text = get_line(); /* get current parse line */
        _start_line = _checked_idx;/* record next line head address */
        printf("got 1 http line : %s\n", text);

        /* main machine state handle & switch */
        switch(_check_state) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parse_request_line(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;   
            }
            case CHECK_STATE_HEADER: {
                ret = parse_headers(text);
                if(ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                if(ret == GET_REQUEST) {
                    return do_request();
                }
                break;   
            }
            case CHECK_STATE_CONTENT: {
                ret = parse_content(text);
                if(ret == GET_REQUEST) {
                    return do_request();
                }
                line_status = LINE_OPEN; /* content is not completed */
                break;   
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

/* parse one line according '\r\n' & replace '\r\n' to '\0\0' */
http_conn::LINE_STATUS http_conn::parse_line() {
    char temp;
    for(; _checked_idx < _read_idx; _checked_idx++) {
        temp = _read_buf[_checked_idx];
        if(temp == '\r') {
            if((_checked_idx + 1) == _read_idx) { /* line is not completed */
                return LINE_OPEN;
            } else if(_read_buf[_checked_idx + 1] == '\n') { /* read '\r\n' */
                _read_buf[_checked_idx++] = '\0';
                _read_buf[_checked_idx++] = '\0';
                return LINE_OK;
            } else {
                return LINE_BAD;
            }
        } else if(temp == '\n') { /* last completed */
            /* _checked_idx can not be the buffer head */
            if(_checked_idx > 1 && _read_buf[_checked_idx - 1] == '\r') {
                _read_buf[_checked_idx - 1] = '\0';
                _read_buf[_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

/* parse request line to get request method, request url, HTTP version */
http_conn::HTTP_CODE http_conn::parse_request_line(char * text) {
     /* url head address */
    _url = strpbrk(text, " \t"); /* find first blank or '\t' in text */
    if(_url == NULL) {
        return BAD_REQUEST;
    }
    /* get request method */
    /* replace blank or '\t' to '\0' */
    /*  eg: GET / HTTP/1.1
        ==> GET\0/ HTTP/1.1 */
    *_url++ = '\0'; 
    /* method head address */
    char *method = text;
    if(strcasecmp(method, "GET") != 0) {
        return BAD_REQUEST;
    } else {
        _method = GET;
    }
    /* get request protocol version */
    /* version head address */
    _version = strpbrk(_url, " \t");
    if(_version == NULL) {
        return BAD_REQUEST;
    }
    *_version++ = '\0';
    if (strcasecmp( _version, "HTTP/1.1") != 0 ) { /* only support HTTP/1.1 */
        return BAD_REQUEST;
    }
    /* method & version is ok */
    /* check url. possible url :
        - http://192.168.110.129:10000/index.html
        - /index.html
    */
    if (strncasecmp(_url, "http://", 7) == 0) {
        _url = strchr(_url + 7, '/');
    }
    if(_url == NULL || *_url != '/') {
        return BAD_REQUEST;
    }
    _check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

/* parse headers to get key-value */
http_conn::HTTP_CODE http_conn::parse_headers(char * text) {
    if(*text == '\0') { /* empty line that means we get a fully headers */
        if(_content_length != 0) { /* there is request content */
            _check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        } 
        return GET_REQUEST;
    } else if(strncasecmp(text, "Connection:", 11) == 0) { /* Connection */
        text += 11;
        text += strspn(text, " \t");
#ifdef __DEBUG
        printf("\n%s\n", text);
#endif
        if(strcasecmp(text, "keep-alive") == 0) {
            _linger = true;
        }
    } else if(strncasecmp(text, "Content-Length:", 15) == 0) { /* request content length */
        text += 15;
        text += strspn(text, " \t");
#ifdef __DEBUG
        printf("\n%s\n", text);
#endif
        _content_length = atol(text);
    } else if(strncasecmp(text, "Host:", 5) == 0) { /* host ip */
        text += 5;
        text += strspn(text, " \t");
#ifdef __DEBUG
        printf("\n%s\n", text);
#endif
        _host = text;
    } else {
        printf( "oop! unknow header %s\n", text);
    }
    return NO_REQUEST;
}

/* parse request content */
http_conn::HTTP_CODE http_conn::parse_content(char * text) {
    /* simple judge content is or not read all. we do not parse it. */
    if(_read_idx >= (_content_length + _checked_idx)) {
        text[_content_length] = '\0';
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/* according parse result to find resource in server & waiting for write to client */
http_conn::HTTP_CODE http_conn::do_request() {
    /* resource file path */
    sprintf(_real_file, "%s%s", DOC_ROOT, _url);
    if(stat(_real_file, &_file_stat) != 0) {
        return BAD_REQUEST;
    }
    /* access : others can read or not */
    if(!(_file_stat.st_mode & S_IROTH)) {
        return BAD_REQUEST;
    }
    /* is or not a dir */
    if(S_ISDIR(_file_stat.st_mode)) {
        return BAD_REQUEST;
    }
    int fd = open(_real_file, O_RDONLY);
    /* create mmap */
    _file_address = (char *)mmap(NULL, _file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd); /* unistd.h */
#ifdef __DEBUG
    if(_file_address == NULL) {
        perror("mmap");
    }
#endif
    return FILE_REQUEST;
}

/* create response content according result code of parse http request */
bool http_conn::process_write(HTTP_CODE http_code) {
    /* hash map will map reponse code to tile (response line) */
    add_status_line(http_code, RESPONSE_CODE_TITLE[http_code]);
    switch (http_code){
        case FILE_REQUEST: {
            add_headers(_file_stat.st_size); 
            /* write buffer */
            _iov[0].iov_base = _write_buf;
            _iov[0].iov_len = _write_idx;
            /* mmap file */
            _iov[1].iov_base = _file_address;
            _iov[1].iov_len = _file_stat.st_size;
            _iovcnt = 2; /* Number of buffers */

            _bytes_to_send = _write_idx + _file_stat.st_size;

#ifdef __DEBUG
    printf("\nbytes to send : %d\n", _bytes_to_send);
#endif
            break;
        }
        case BAD_REQUEST :
        case FORBIDDEN_REQUEST:
        case NO_RESOURCE : 
        case INTERNAL_ERROR : {
            add_headers(strlen(RESPONSE_CODE_FORM[http_code])); /* get string length use strlen not sizeof */
            if(add_content(RESPONSE_CODE_FORM[http_code]) == false) {
                return false;
            }
            /* write buffer */
            _iov[0].iov_base = _write_buf;
            _iov[0].iov_len = _write_idx;
            _iovcnt = 1; /* Number of buffers */

            _bytes_to_send = _write_idx;
            break;
        }
        default:{
            return false;
        }
    }
    return true;
}

/* response line */
bool http_conn::add_status_line(int status, const char *title) {
#ifdef __DEBUG
    printf("\nadd status line...\n");
#endif
    return add_reponse("%s %d %s\r\n", "HTTP/1.1", status, title);
}

/* response headers */
bool http_conn::add_headers(int content_len) {
#ifdef __DEBUG
    printf("\nadd headers...\n");
#endif
    return (add_content_length(content_len) &&
        add_content_type() &&
        add_linger() &&
        add_blank_line());
}

/* response content */
bool http_conn::add_content(const char *content) {
#ifdef __DEBUG
    printf("\nadd content...\n");
#endif
    add_reponse("%s", content);
}

/* response headers : Content-Length */
bool http_conn::add_content_length(int content_len) {
    return add_reponse("Content-Length: %d\r\n", content_len);
}

/* response headers : Content-Type */
bool http_conn::add_content_type() {
    return add_reponse("Content-Type: %s\r\n", "text/html");
}

/* response headers : Connection keep-alive or close */
bool http_conn::add_linger() {
    return add_reponse("Connection: %s\r\n", (_linger == true ? "keep-alive" : "close"));
}

/* '\r\n' */
bool http_conn::add_blank_line() {
    return add_reponse("%s", "\r\n");
}

/* write data to write buffer write for sending */
bool http_conn::add_reponse(const char* format, ... ) {
    if(_write_idx >= WRITE_BUFFER_SIZE) { /* write buffer is full */
        return false;
    }
    va_list args_list;
    va_start(args_list, format); /* create va_list ob */

    int cur_write_size = WRITE_BUFFER_SIZE - 1 - _write_idx;
    int write_len = vsnprintf(_write_buf + _write_idx, cur_write_size, format, args_list);
    if(write_len >= cur_write_size) {
        return false;
    }
    _write_idx += write_len; /* update write index */
    va_end(args_list); /* release va_list ob */
    
    return true;
}

}