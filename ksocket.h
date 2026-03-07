#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

int k_socket(int domain, int type, int protocol);

int k_bind(int sockfd, struct sockaddr* src, struct sockaddr* dest);

ssize_t k_sendto(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen);

ssize_t k_recvfrom(int sockfd, const void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t *addrlen);

int close(int fd);