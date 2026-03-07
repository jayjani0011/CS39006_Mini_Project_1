#include "ksocket.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int k_socket(int domain, int type, int protocol) {
    if (domain != AF_INET || type != SOCK_KTP || protocol != 0) {
        return -1; // invalid arguments
    }
    // find a free entry in SM and allocate a new socket
    for (int i = 0; i < N; i++) {
        Wait(semid, i);
        if (SM[i].isfree) {
            SM[i].isfree = false;
            SM[i].udpsockfd = socket(AF_INET, SOCK_DGRAM, 0); // assign sockfd as index in SM
            SM[i].pid = getpid(); // to get the pid of the calling process

            printf("Socket created with k_sockfd : %d\n", i);

            Signal(semid, i);
            return i;
        }
        Signal(semid, i);
    }

    errno = ENOSPACE;
    return -1; // no free entry found
}

int k_bind(int sockfd, struct sockaddr* src, struct sockaddr* dest) {
    Wait(semid, sockfd);
    if (sockfd < 0 || sockfd >= N || SM[sockfd].isfree) {
        Signal(semid, sockfd);
        return -1; // invalid socket
    }

    // bind the socket to the source and destination addresses
    bind(SM[sockfd].udpsockfd, src, sizeof(*src));
    SM[sockfd].dest = *((struct sockaddr_in*) dest);
    Signal(semid, sockfd);
    return 0;
}

ssize_t k_sendto(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen) {
    Wait(semid, sockfd);
    if (sockfd < 0 || sockfd >= N || SM[sockfd].isfree) {
        Signal(semid, sockfd);
        return -1; // invalid socket
    }

    // check if the destination address matches the one bound to the socket
    struct sockaddr_in* dest = (struct sockaddr_in*) dest_addr;
    if (dest->sin_port != SM[sockfd].dest.sin_port || dest->sin_addr.s_addr != SM[sockfd].dest.sin_addr.s_addr) {
        // drop this message and return an error
        errno = ENOTBOUND;
        Signal(semid, sockfd);
        return -1; // destination address mismatch
    }

    // check if the sender window is full
    if ((SM[sockfd].swnd.start + 1) % BUF_SIZE == SM[sockfd].swnd.end) {
        // drop this message and return an error
        errno = ENOSPACE;
        Signal(semid, sockfd);
        return -1; // sender window is full
    }

    // add the message to the sender window
    int idx = SM[sockfd].swnd.end;
    strncpy(SM[sockfd].send_buf[idx], (char*) buf, len);
    SM[sockfd].swnd.end = (SM[sockfd].swnd.end + 1) % BUF_SIZE;

    // send the message to the destination address
    // sendto(SM[sockfd].udpsockfd, buf, len, flags, dest_addr, addrlen);
    Signal(semid, sockfd);
    return len;
}

ssize_t k_recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t *addrlen) {
    Wait(semid, sockfd);
    if (sockfd < 0 || sockfd >= N || SM[sockfd].isfree) {
        Signal(semid, sockfd);
        return -1; // invalid socket
    }

    // check if the receiver window is empty
    if (SM[sockfd].rwnd.start == SM[sockfd].rwnd.end) {
        // no message to receive, return an error
        errno = ENOMESSAGE;
        Signal(semid, sockfd);
        return -1; // receiver window is empty
    }

    // get the message from the receiver window
    int idx = SM[sockfd].rwnd.start;
    strncpy((char*) buf, SM[sockfd].recv_buf[idx], len);
    SM[sockfd].rwnd.start = (SM[sockfd].rwnd.start + 1) % BUF_SIZE;

    // set the source address and address length
    // struct sockaddr_in* src = (struct sockaddr_in*) src_addr;
    // *src = SM[sockfd].dest; // source address is the destination address bound to the socket
    // *addrlen = sizeof(*src);

    Signal(semid, sockfd);
    return len;
}

int close(int fd) {
    Wait(semid, fd);
    if (fd < 0 || fd >= N) {
        Signal(semid, fd);
        return -1; // invalid socket
    }

    // close the corresponding UDP socket and mark this entry as free
    if (!SM[fd].isfree) close(SM[fd].udpsockfd);
    SM[fd].isfree = true;
    Signal(semid, fd);
    return 0;
}

bool dropMessage(float p) {
    return ((float) rand() / RAND_MAX) < p; // return true with probability p
}