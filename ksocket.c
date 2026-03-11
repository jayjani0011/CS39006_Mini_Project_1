#include "ksocket.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int semid, shmid;
sockinfo* SM; // shared memory for storing sockinfo of all sockets

int k_socket(int domain, int type, int protocol) {
    if (domain != AF_INET || type != SOCK_KTP || protocol != 0) {
        return -1;
    }

    printf("k_socket called with domain = %d, type = %d, protocol = %d\n", domain, type, protocol);

    if (!SM) {
        printf("Shared memory is not created\n");
        return -1;
    }

    for (int i = 0; i < N; i++) {
        printf("k_socket: Checking socket index %d, isfree = ", i);
        fflush(stdout);
        printf("%d\n", SM[i].isfree);
        fflush(stdout);
        Wait(semid, i);
        if (SM[i].isfree) {
            // int fd = socket(AF_INET, SOCK_DGRAM, 0);
            // if (fd < 0) {
            //     Signal(semid, i);
            //     return -1;
            // }

            SM[i].isfree = false;
            SM[i].nospace = 0;
            SM[i].last_ack_time = -1;
            // SM[i].udpsockfd = fd;
            SM[i].pid = getpid();
            SM[i].swnd = init_window();
            SM[i].rwnd = init_window();
            printf("Socket created with k_sockfd : %d\n", i);

            Signal(semid, i);
            return i;
        }
        Signal(semid, i);
    }
    errno = ENOSPACE;
    return -1;
}

int k_bind(int sockfd, struct sockaddr* src, struct sockaddr* dest) {
    Wait(semid, sockfd);
    if (sockfd < 0 || sockfd >= N || SM[sockfd].isfree) {
        Signal(semid, sockfd);
        return -1;
    }
    // bind(SM[sockfd].udpsockfd, src, sizeof(struct sockaddr_in));
    SM[sockfd].isbound = true;
    SM[sockfd].dest = *((struct sockaddr_in*)dest);
    SM[sockfd].src = *((struct sockaddr_in*)src);
    printf("Binding k_sockfd %d to Dest IP : %s, PORT : %d\n", sockfd, inet_ntoa(SM[sockfd].dest.sin_addr), ntohs(SM[sockfd].dest.sin_port));
    Signal(semid, sockfd);
    return 0;
}

ssize_t k_sendto(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen) {
    if (sockfd < 0 || sockfd >= N) {
        errno = EBADF;
        return -1;
    }

    Wait(semid, sockfd);
    if (SM[sockfd].isfree) {
        Signal(semid, sockfd);
        return -1;
    }
    struct sockaddr_in* dest = (struct sockaddr_in*)dest_addr;
    if (!SM[sockfd].isbound || dest->sin_port != SM[sockfd].dest.sin_port || dest->sin_addr.s_addr != SM[sockfd].dest.sin_addr.s_addr) {
        printf("k_sendto: Socket %d not bound to the specified destination\n", sockfd);
        errno = ENOTBOUND;
        Signal(semid, sockfd);
        return -1;
    }

    if ((SM[sockfd].swnd.end + 1) % BUF_SIZE == SM[sockfd].swnd.start) {
        printf("k_sendto: Socket %d sender window full, cannot send new packet\n", sockfd);
        errno = ENOSPACE;
        Signal(semid, sockfd);
        return -1;
    }

    int idx = SM[sockfd].swnd.end;
    strcpy(SM[sockfd].send_buf[idx], (char*)buf);
    SM[sockfd].send_buf[idx][len] = '\0';
    SM[sockfd].swnd.end = (SM[sockfd].swnd.end + 1) % BUF_SIZE;
    printf("k_sendto : Buffered message for k_socket %d at buffer index %d, msg : \n%s\n", sockfd, idx, SM[sockfd].send_buf[idx]);
    Signal(semid, sockfd);

    return len;
}

ssize_t k_recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t *addrlen) {
    Wait(semid, sockfd);
    if (sockfd < 0 || sockfd >= N || SM[sockfd].isfree) {
        Signal(semid, sockfd);
        return -1;
    }
    if (SM[sockfd].rwnd.size == WINDOW_SIZE - 1 || SM[sockfd].rwnd.start == SM[sockfd].rwnd.end) {
        printf("k_recvfrom: Socket %d receiver window empty, no message to receive\n", sockfd);
        errno = ENOMESSAGE;
        Signal(semid, sockfd);
        return -1;
    }

    int idx = SM[sockfd].rwnd.start;
    strcpy((char*)buf, SM[sockfd].recv_buf[idx]);
    printf("k_recvfrom: Received message for k_socket %d from buffer index %d, msg : \n%s\n", sockfd, idx, (char*)buf);
    SM[sockfd].rwnd.start = (SM[sockfd].rwnd.start + 1) % BUF_SIZE;
    SM[sockfd].rwnd.size++;   // free space increased

    Signal(semid, sockfd);

    return strlen((char*)buf);
}

int k_close(int fd){
    if (fd < 0 || fd >= N) return -1;

    Wait(semid, fd);
    if (SM[fd].isfree) {
        Signal(semid, fd);
        return -1;
    }

    // close UDP socket
    if (SM[fd].udpsockfd >= 0) close(SM[fd].udpsockfd);

    // reset socket state
    SM[fd].isfree = true;
    SM[fd].isbound = false;
    SM[fd].pid = -1;
    SM[fd].udpsockfd = -1;
    SM[fd].nospace = false;

    memset(SM[fd].send_buf, 0, sizeof(SM[fd].send_buf));
    memset(SM[fd].recv_buf, 0, sizeof(SM[fd].recv_buf));

    // reset sender window
    SM[fd].swnd.start = 0;
    SM[fd].swnd.end = 0;
    SM[fd].swnd.last_ack = 0;
    SM[fd].swnd.last_seq = 0;

    // reset receiver window
    SM[fd].rwnd.start = 0;
    SM[fd].rwnd.end = 0;
    SM[fd].rwnd.size = BUF_SIZE;

    Signal(semid, fd);
    return 0;
}

bool dropMessage(float p) {
    srand(time(NULL));
    return ((float)rand() / RAND_MAX) < p;
}

window init_window() {
    window W;
    W.start = 0;
    W.end = 0;
    W.size = WINDOW_SIZE - 1;
    W.last_ack = 0;
    W.last_seq = 0;
    
    for (int i = 0; i < BUF_SIZE; i++) {
        W.seq_num[i] = 0; // initialize sequence numbers to 0 to indicate empty slots
        W.timestamp[i] = -1;
    }

    return W;
}