#ifndef TOOLS_H
#define TOOLS_H

#include <sys/epoll.h>
#include <fcntl.h>
#include <stdio.h> 
#include <unistd.h>
#include <signal.h>
#include <cassert>
#include <string.h> 
#include <sys/socket.h> 

namespace lu {

class tools {
public:
    /* set signal catch */
    static void set_sigcatch(int signum, void(*handler)(int), bool restart = true);
    /* send info to connfd */
    static void show_err(int connfd, const char *info);
    /* set fd no blocking */
    static int set_nonblocking(int fd);
    /* add fd to epoll, is or not open EPOLLONESHOT according to oneshot */
    static void addfd(int epollfd, int fd, bool oneshot);
    /* remove fd */
    static void removefd(int epollfd, int fd);
    /* modify fd */
    static void modifyfd(int epollfd, int fd, int ev);
};

}
#endif