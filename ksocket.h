/*
=====================================
Mini Project 1 Submission
Group Details:
Member 1 Name: Jay Jani
Member 1 Roll number: 23CS10027
Member 2 Name: Shresth Jha
Member 2 Roll number: 23CS30024
=====================================
*/

#pragma once

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
#include <stdint.h>

#define SOCK_KTP 101
#define BUF_SIZE 11
#define WINDOW_SIZE BUF_SIZE
#define MSG_SIZE 512
#define MSG_TYPE 4
#define SEQ_NUM_MOD 256
#define KTP_HEADER_SIZE (MSG_TYPE + 2*sizeof(uint8_t))
#define N 10
#define T 5
#define P 0.3

#define ENOSPACE ENOSPC
#define ENOTBOUND ENOTCONN
#define ENOMESSAGE ENOMSG

typedef struct {
    int start;
    int end;
    int size;
    uint8_t seq_num[BUF_SIZE]; // sequence number for each slot in the window
    // expected sequence number for the next message to be added to each slot in the window for rwnd
    // contains the sequence numbers of the messages sent, but not yet acknowledged for swnd
    uint8_t last_ack; // sequence number of the last acknowledged message for swnd
    uint8_t last_seq; // sequence number of the last sent message for swnd
    time_t timestamp[BUF_SIZE]; // timestamp for each slot in the window, used for retransmission timeout
} window;

typedef struct {
    int udpsockfd;
    int pid;
    bool isfree;
    bool isbound;
    struct sockaddr_in src;
    struct sockaddr_in dest;
    char send_buf[BUF_SIZE][MSG_SIZE + 1];
    char recv_buf[BUF_SIZE][MSG_SIZE + 1];
    window swnd;
    window rwnd;
    int nospace;        // receiver buffer previously full
    uint8_t last_ack_sent;
    time_t last_ack_time;
} sockinfo;

int k_socket(int domain, int type, int protocol);

int k_bind(int sockfd, struct sockaddr* src, struct sockaddr* dest);

ssize_t k_sendto(int sockfd, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr, socklen_t addrlen);

ssize_t k_recvfrom(int sockfd, void* buf, size_t len, int flags, struct sockaddr* src_addr, socklen_t *addrlen);

int k_close(int fd);

bool dropMessage(float p);

window init_window();

static inline void _sem_op(int semid, int idx, int op) {
    struct sembuf sb = {(unsigned short)idx, (short)op, 0};
    semop(semid, &sb, 1);
}

#define Wait(semid, idx) _sem_op((semid), (idx), -1)
#define Signal(semid, idx) _sem_op((semid), (idx), +1)