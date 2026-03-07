#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define SOCK_KTP 101
#define BUF_SIZE 10
#define WINDOW_SIZE 10
#define MSG_SIZE 512
#define N 10
#define T 5
#define P 0.3

const int SEM_KEY = ftok(".", 'S');
const int SHM_KEY = ftok(".", 'J');

typedef struct {
    int start;
    int end;
    char messages[BUF_SIZE][MSG_SIZE];
} window;

typedef struct {
    int sockfd; // corresponding UDP socket file descriptor
    int pid;
    int isfree = 1;
    // sockaddr_in src;
    sockaddr_in dest;
    char send_buf[BUF_SIZE][MSG_SIZE];
    char recv_buf[BUF_SIZE][MSG_SIZE];
    window swnd; // sender window
    window rwnd; // receiver window
} sockinfo;

sockinfo* SM; // shared memory for storing sockinfo of all sockets

int k_socket(int domain, int type, int protocol);

int k_bind(int sockfd, struct sockaddr* src, struct sockaddr* dest);

ssize_t k_sendto(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen);

ssize_t k_recvfrom(int sockfd, const void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t *addrlen);

int close(int fd);