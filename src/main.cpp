#include <sys/socket.h> 
#include <netinet/in.h> 
#include <arpa/inet.h> 
#include <unistd.h> 
#include <errno.h> 
#include <string.h> 
#include <fcntl.h> 
#include <stdlib.h> 
#include <cassert> 
#include <sys/epoll.h> 

#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#include "tools.h"

#define MAX_FD 65536
#define MAX_EVENT_NUMBER 10000
#define BACKLOG_DEFAULT 64
#define NUMBER_IGN 1

int main(int argc, char *argv[]) {
    const char *ip = NULL;
    int port;
    if(argc < 2) {
        printf("usage 1 : %s <ip-address> <port-number>\n", basename(argv[0]));
        printf("usage 2 : %s <port-number>\n", basename(argv[0]));
        exit(-1);
    }
    ip = argc == 2 ? "192.168.1.111" : argv[1];
    port = atoi(argv[argc - 1]);
    
    /* ignore SIGPIPE */ 
    lu::tools::set_sigcatch(SIGPIPE, SIG_IGN);

    /* create thread pool of http connction */
    lu::threadpool<lu::http_conn> *conn_pool = NULL;
    try {
        conn_pool = new lu::threadpool<lu::http_conn>;
    } catch(const std::exception& e) {
        return -1;
    }
    
    /* possible users' http connction */
    lu::http_conn *users = new lu::http_conn[MAX_FD];
    assert(users != NULL);

    /* listen fd */
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    /* set address reuse */
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    int ret = 0;

    /* bind address */
    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &server_addr.sin_addr);
    server_addr.sin_port = htons(port);
    ret = bind(listenfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    assert(ret >= 0);

    /* listen */
    ret = listen(listenfd, BACKLOG_DEFAULT);
    assert(ret >= 0);

    /* epoll */
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(NUMBER_IGN); /* the size argument is ignored, but must be greater than zero */
    assert(epollfd >= 0);

    lu::tools::addfd(epollfd, listenfd, false);
    lu::http_conn::_epollfd = epollfd; /* mark epoll fd in http connction */

    while(true) {
        /* waitting for events comming */
#ifdef __DEBUG
        printf("wait...\n");
#endif
        int num = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if((num < 0) && (errno != EINTR)) {
            printf("epoll failure\n");
            break;
        }
        
        /* traverse events */
        for(int i = 0; i < num; i++) {
            int curfd = events->data.fd;
            if(listenfd == curfd) {
                /* new connction comming */
#ifdef __DEBUG
                printf("new connction...\n");
#endif
                struct sockaddr_in client_addr; 
                socklen_t client_addr_len = sizeof(client_addr);
                int connfd = accept(listenfd, (struct sockaddr *)&client_addr, &client_addr_len);
                if(connfd < 0) {
                    printf("errno is: %d", errno);
                    perror(" ");
                    continue;
                }
                if(lu::http_conn::_user_count >= MAX_FD) {
                    lu::tools::show_err(connfd, "Server busy");
                    continue;
                }
                /* initialize client connction */
                users[connfd].init(connfd, client_addr);
            } else if(events[i].events & (EPOLLIN)) {
                /* read events ready */
                if(users[curfd].read()) {
                    conn_pool->append(&users[curfd]);
                } else {
                    users[curfd].close();
                }
            } else if(events[i].events & (EPOLLOUT)) {
                /* write events ready */    
                if(!users[curfd].write()) {
                    users[curfd].close();
                }
            } else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) { 
                /* error */
                users[curfd].close();
            }
        }
    }

    /* release resource */
    close(epollfd);
    close(listenfd);
    delete [] users;
    delete conn_pool;

    return 0;
}