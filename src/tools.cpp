#include "tools.h"

namespace lu {

/* set signal catch */
void tools::set_sigcatch(int signum, void(*handler)(int), bool restart) {
    struct sigaction act;
    bzero(&act, sizeof(act));
    if(restart) {
        act.sa_flags |= SA_RESTART; /* recall the interrupted system call by the signal */
    }
    act.sa_handler = handler;
    sigfillset(&act.sa_mask);
    /* if -1, assert */
    assert(sigaction(signum, &act, NULL) != -1);
}

/* send info to connfd */
void tools::show_err(int connfd, const char *info) {
    printf("%s\n", info);
    send(connfd, info, strlen(info), 0);
    close(connfd);
}

int tools::set_nonblocking(int fd) {
    int old_flags = fcntl(fd, F_GETFL);
    int new_flags = old_flags | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flags);
    return old_flags;
}

/* add fd to epoll, is or not open EPOLLONESHOT according to oneshot */
void tools::addfd(int epollfd, int fd, bool oneshot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if(oneshot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    set_nonblocking(fd);
}

/* remove fd */
void tools::removefd(int epollfd, int fd) {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

/* modify fd */
void tools::modifyfd(int epollfd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

}