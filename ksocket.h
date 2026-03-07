#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <stdbool.h>

#define SOCK_KTP 101
#define BUF_SIZE 10
#define WINDOW_SIZE 10
#define MSG_SIZE 512
#define N 10
#define T 5
#define P 0.3

#define ENOSPACE ENOSPC
#define ENOTBOUND ENOTCONN
#define ENOMESSAGE ENOMSG

int semid;
int shmid;

typedef struct {
    int start;
    int end;
    int seq_num[BUF_SIZE]; // sequence number for each slot in the window
    // expected sequence number for the next message to be added to each slot in the window for rwnd
    // contains the sequence numbers of the messages sent, but not yet acknowledged for swnd
    int last_ack; // sequence number of the last acknowledged message for swnd
    int last_seq; // sequence number of the last sent message for swnd
} window;

typedef struct {
    int udpsockfd; // corresponding UDP socket file descriptor
    int pid;
    bool isfree; // 1 if this entry is free, 0 otherwise
    // sockaddr_in src;
    struct sockaddr_in dest;
    char send_buf[BUF_SIZE][MSG_SIZE];
    char recv_buf[BUF_SIZE][MSG_SIZE];
    window swnd; // sender window
    window rwnd; // receiver window
} sockinfo;

sockinfo* SM; // shared memory for storing sockinfo of all sockets

int k_socket(int domain, int type, int protocol);

int k_bind(int sockfd, struct sockaddr* src, struct sockaddr* dest);

ssize_t k_sendto(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen);

ssize_t k_recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t *addrlen);

int close(int fd);

bool dropMessage(float p);

static inline void _sem_op(int semid, int idx, int op) {
    struct sembuf sb = {(unsigned short)idx, (short)op, 0};
    semop(semid, &sb, 1);
}

#define Wait(semid, idx) _sem_op((semid), (idx), -1)
#define Signal(semid, idx) _sem_op((semid), (idx), +1)