/* anet.c -- Basic TCP socket stuff made a bit less boring
 * Copyright (C) 2006-2009 Salvatore Sanfilippo <antirez@invece.org> */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include "net.h"

static void netSetError(char *err, const char *fmt, ...)
{
	if (!err) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, NET_ERR_LEN, fmt, ap);
    va_end(ap);
}

int netNonBlock(char *err, int fd)
{
    /* Set the socket nonblocking.
     * Note that fcntl(2) for F_GETFL and F_SETFL can't be
     * interrupted by a signal. */
	int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        netSetError(err, "fcntl(F_GETFL): %s\n", strerror(errno));
        return NET_ERR;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        netSetError(err, "fcntl(F_SETFL,O_NONBLOCK): %s\n", strerror(errno));
        return NET_ERR;
    }
    return NET_OK;
}

int netTcpNoDelay(char *err, int fd)
{
    int on = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on)) == -1) {
        netSetError(err, "setsockopt TCP_NODELAY: %s\n", strerror(errno));
        return NET_ERR;
    }
    return NET_OK;
}

int netTcpServer(char *err, int port, char *bindaddr)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        netSetError(err, "socket: %s\n", strerror(errno));
        return NET_ERR;
    }

    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) == -1) {
        netSetError(err, "setsockopt SO_REUSEADDR: %s\n", strerror(errno));
        close(fd);
        return NET_ERR;
    }

    struct sockaddr_in sa;
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bindaddr) inet_aton(bindaddr, &sa.sin_addr);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
        netSetError(err, "bind: %s\n", strerror(errno));
        close(fd);
        return NET_ERR;
    }

    if (listen(fd, 5) == -1) {
        netSetError(err, "listen: %s\n", strerror(errno));
        close(fd);
        return NET_ERR;
    }
    return fd;
}

int netAccept(char *err, int serversock, char *ip, int *port)
{
    int fd;
    struct sockaddr_in sa;
    while (1) {
    	unsigned int saLen = sizeof(sa);
        fd = accept(serversock, (struct sockaddr *)&sa, &saLen);
        if (fd == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                netSetError(err, "accept: %s\n", strerror(errno));
                return NET_ERR;
            }
        }
        break;
    }
    if (ip) strcpy(ip, inet_ntoa(sa.sin_addr));
    if (port) *port = ntohs(sa.sin_port);
    return fd;
}
