#ifndef NET_H
#define NET_H

#define NET_OK 0
#define NET_ERR -1
#define NET_ERR_LEN 256

int netNonBlock(char *err, int fd);
int netTcpNoDelay(char *err, int fd);
int netTcpServer(char *err, int port, char *bindaddr);
int netAccept(char *err, int serversock, char *ip, int *port);

#endif
