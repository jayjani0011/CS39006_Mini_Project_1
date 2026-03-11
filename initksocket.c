#include "ksocket.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>

// HEADER : 6 bytes
//  4 bytes : "DATA" or "ACK\0"
//  1 byte : SEQ NUM
//  1 byte : RWND SIZE

extern int semid, shmid;
extern sockinfo* SM;

void build_packet(char *packet, const char *type, uint8_t seq, uint8_t rwnd, const char *msg) {
    memcpy(packet, type, MSG_TYPE);
    memcpy(packet + MSG_TYPE, &seq, sizeof(uint8_t));
    memcpy(packet + MSG_TYPE + sizeof(uint8_t), &rwnd, sizeof(uint8_t));
    if (strcmp(type, "ACK")) memcpy(packet + KTP_HEADER_SIZE, msg, MSG_SIZE);
}

void parse_packet(const char *packet, char *type, uint8_t *seq, uint8_t *rwnd, char *msg) {
    memcpy(type, packet, MSG_TYPE);
    type[MSG_TYPE] = '\0';
    memcpy(seq, packet + MSG_TYPE, sizeof(uint8_t));
    memcpy(rwnd, packet + MSG_TYPE + sizeof(uint8_t), sizeof(uint8_t));
    if (strcmp(type, "ACK")) {
        memcpy(msg, packet + KTP_HEADER_SIZE, MSG_SIZE);
        msg[MSG_SIZE] = '\0';
    }
}

int window_count(window *W) {
    return (W->end - W->start + BUF_SIZE) % BUF_SIZE;
}

void send_ack(int sockindex, uint8_t seq) {
    char packet[KTP_HEADER_SIZE];
    build_packet(packet, "ACK\0", seq, SM[sockindex].rwnd.size, NULL);
    sendto(SM[sockindex].udpsockfd, packet, KTP_HEADER_SIZE, 0, (struct sockaddr *)&SM[sockindex].dest, sizeof(SM[sockindex].dest));
}

void retransmit_packet(int sockindex, int bufindex) {
    char packet[KTP_HEADER_SIZE + MSG_SIZE];
    uint8_t seq = SM[sockindex].swnd.seq_num[bufindex];
    build_packet(packet, "DATA", seq, SM[sockindex].rwnd.size, SM[sockindex].send_buf[bufindex]);
    sendto(SM[sockindex].udpsockfd, packet, sizeof(packet), 0, (struct sockaddr *)&SM[sockindex].dest, sizeof(SM[sockindex].dest));
    SM[sockindex].swnd.timestamp[bufindex] = time(NULL);
}

// returns next sequence number, handling wrap around
// never use 0 as a valid sequence number, since we initialize all seq_num in window to 0 to indicate empty slot
uint8_t next_seq(int seq) {
    if (seq == (uint8_t)(-1)) return 1;
    return (uint8_t)(seq + 1);
}

void send_new_packet(int sockindex, int bufindex) {
    char packet[KTP_HEADER_SIZE + MSG_SIZE];
    SM[sockindex].swnd.last_seq = next_seq(SM[sockindex].swnd.last_seq);
    uint8_t seq = SM[sockindex].swnd.last_seq;
    SM[sockindex].swnd.seq_num[bufindex] = seq;
    build_packet(packet, "DATA", seq, SM[sockindex].rwnd.size, SM[sockindex].send_buf[bufindex]);
    sendto(SM[sockindex].udpsockfd, packet, sizeof(packet), 0, (struct sockaddr *)&SM[sockindex].dest, sizeof(SM[sockindex].dest));
    SM[sockindex].swnd.timestamp[bufindex] = time(NULL);
}

void slide_sender_window(int sockindex, uint8_t ack_seq) {
    window *W = &SM[sockindex].swnd;
    while (W->start != W->end && W->seq_num[W->start] != next_seq(ack_seq) && W->seq_num[W->start] != 0) {
        int idx = W->start;
        W->seq_num[idx] = 0;
        W->timestamp[idx] = -1;
        memset(SM[sockindex].send_buf[idx], 0, MSG_SIZE);
        W->start = (W->start + 1) % BUF_SIZE;
    }
    W->last_ack = ack_seq;
}

void *threadR() {
    fd_set readfds;
    struct timeval timeout;
    printf("Receiver thread started\n");

    while (1) {
        printf("Receiver thread awake, checking for incoming packets\n");
        // Build fd set
        FD_ZERO(&readfds);
        int maxfd = -1;
        for (int i = 0; i < N; i++) {
            Wait(semid, i);
            if (!SM[i].isfree && SM[i].isbound) {
                if (SM[i].udpsockfd == -1) {
                    SM[i].udpsockfd = socket(AF_INET, SOCK_DGRAM, 0);
                    printf("Created UDP socket fd %d for k_sockfd %d\n", SM[i].udpsockfd, i);
                    if (SM[i].udpsockfd < 0) {
                        perror("socket");
                        Signal(semid, i);
                        continue;
                    }
                    if (bind(SM[i].udpsockfd, (struct sockaddr *)&SM[i].src, sizeof(SM[i].src)) < 0) {
                        perror("bind");
                        Signal(semid, i);
                        continue;
                    } // bind to source address
                    printf("Bound UDP socket fd %d for k_sockfd %d to Source IP : %s, PORT : %d\n", SM[i].udpsockfd, i, inet_ntoa(SM[i].src.sin_addr), ntohs(SM[i].src.sin_port));
                }
                FD_SET(SM[i].udpsockfd, &readfds);
                if (SM[i].udpsockfd > maxfd) maxfd = SM[i].udpsockfd;
            }
            Signal(semid, i);
        }

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        int activity = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
        if (activity < 0) {
            perror("select");
            continue;
        }

        // SELECT TIMEOUT

        // check duplicate ACK sending condition
        if (activity == 0) {
            for (int i = 0; i < N; i++) {
                Wait(semid, i);
                time_t now = time(NULL);
                if (!SM[i].isfree && SM[i].isbound && SM[i].nospace && SM[i].rwnd.size > 0 && SM[i].last_ack_time != -1 && difftime(now, SM[i].last_ack_time) >= SM[i].nospace * T) {
                    char packet[KTP_HEADER_SIZE];
                    build_packet(packet, "ACK\0", SM[i].last_ack_sent, SM[i].rwnd.size, NULL);
                    sendto(SM[i].udpsockfd, packet, KTP_HEADER_SIZE, 0, (struct sockaddr *)&SM[i].dest, sizeof(SM[i].dest));
                    SM[i].last_ack_time = time(NULL);
                    SM[i].nospace++; // this duplicate may get dropped as well (that problem written in assignment)
                    // hence, we need to keep sending ACK's with increasing timeout values, until we receive a message
                }
                Signal(semid, i);
            }
            continue;
        }

        // DATA or ACK arrived
        for (int i = 0; i < N; i++) {
            Wait(semid, i);
            if (!SM[i].isfree && SM[i].isbound && FD_ISSET(SM[i].udpsockfd, &readfds)) {
                char packet[KTP_HEADER_SIZE + MSG_SIZE];
                struct sockaddr_in src;
                socklen_t addrlen = sizeof(src);

                int recvlen = recvfrom(SM[i].udpsockfd, packet, sizeof(packet), 0, (struct sockaddr *)&src, &addrlen);
                if (recvlen < 0) {
                    perror("recvfrom");
                    Signal(semid, i);
                    continue;
                }

                char type[MSG_TYPE + 1], msg[MSG_SIZE + 1];
                uint8_t seq, rwnd;
                msg[0] = '\0';
                parse_packet(packet, type, &seq, &rwnd, msg);

                printf("Packet received on k_socket %d: type = %s, seq = %d, rwnd = %d, msg = \n%s\n", i, type, seq, rwnd, msg);

                // simulate unreliable channel
                if (dropMessage(P)) {
                    printf("Packet dropped: type = %s, seq = %d, rwnd = %d\n", type, seq, rwnd);
                    Signal(semid, i);
                    continue;
                }

                // verify correct peer
                if (src.sin_addr.s_addr != SM[i].dest.sin_addr.s_addr || src.sin_port != SM[i].dest.sin_port) {
                    Signal(semid, i);
                    continue;
                }

                // ACK RECEIVED
                if (strcmp(type, "ACK") == 0) {
                    // update sender window size
                    SM[i].swnd.size = rwnd;
                    // slide sender window only if this ACK advances it
                    printf("ACK received for seq %d on k_socket %d, sender window size updated to %d, last acknowledged seq = %d\n", seq, i, rwnd, SM[i].swnd.last_ack);
                    if ((uint8_t)(seq - SM[i].swnd.last_ack) < WINDOW_SIZE) {
                        int st = SM[i].swnd.start;
                        slide_sender_window(i, seq);
                        int nst = SM[i].swnd.start;
                        printf("Slided swnd for k_sockfd : %d from start = %d to start = %d\n", i, st, nst);
                    }
                }
                // DATA RECEIVED
                else if (strcmp(type, "DATA") == 0) {
                    window *W = &SM[i].rwnd;
                    // drop packet if receiver buffer full
                    if (W->size == 0) {
                        SM[i].nospace = 1;
                        Signal(semid, i);
                        continue;
                    }

                    SM[i].nospace = 0; // this will stop sending duplicate ACK's

                    uint8_t expected = next_seq(SM[i].last_ack_sent);
                    // check if packet is duplicate
                    for (int k = W->start; k != W->end; k = (k + 1) % BUF_SIZE) {
                        if (W->seq_num[k] == seq) {
                            Signal(semid, i);
                            continue;
                        }
                    }

                    // compute offset from expected seq (wrap safe)
                    uint8_t diff = (uint8_t)(seq - expected);
                    // if packet outside receiver window -> drop
                    // need to send duplicate ACK for duplicate packet
                    if (diff >= W->size - 1) {
                        printf("Packet with seq %d outside receiver window, expected %d\n", seq, expected);
                        if ((uint8_t)(seq - SM[i].last_ack_sent) < WINDOW_SIZE) {
                            printf("Packet with seq %d is a new packet outside receiver window, sending ACK for last acknowledged seq %d\n", seq, SM[i].last_ack_sent);
                            char packet[KTP_HEADER_SIZE];
                            build_packet(packet, "ACK\0", SM[i].last_ack_sent, SM[i].rwnd.size, NULL);
                            sendto(SM[i].udpsockfd, packet, KTP_HEADER_SIZE, 0, (struct sockaddr *)&SM[i].dest, sizeof(SM[i].dest));
                            SM[i].last_ack_time = time(NULL);
                        }
                        Signal(semid, i);
                        continue;
                    }

                    int idx = (W->end + diff) % BUF_SIZE;
                    // store packet
                    strncpy(SM[i].recv_buf[idx], msg, strlen(msg));
                    W->seq_num[idx] = seq;
                    W->size--;
                    if (W->size == 0) SM[i].nospace = true;
                    printf("Stored packet with seq %d at recv buffer index %d for k_socket %d\n", seq, idx, i);
                    
                    // if this is the next expected packet
                    if (seq == expected) {
                        // slide window through contiguous packets
                        while (W->seq_num[W->end] != 0) {
                            int idx = W->end;
                            SM[i].last_ack_sent = W->seq_num[idx];
                            printf("Sliding receiver window, ACKing seq %d for k_socket %d\n", SM[i].last_ack_sent, i);
                            W->seq_num[idx] = 0; // will this create issue for duplicate ACK sending?
                            W->end = (W->end + 1) % BUF_SIZE;
                        }

                        send_ack(i, SM[i].last_ack_sent);
                        SM[i].last_ack_time = time(NULL);
                        printf("Sent ACK for seq %d for k_socket %d\n", SM[i].last_ack_sent, i);
                    }
                    // out-of-order packets are stored but NOT ACKed
                }
            }
            Signal(semid, i);
        }
    }
}

void *threadS() {
    printf("Sender thread started\n");

    while (1) {
        // sleep for T / 2 seconds
        sleep(T / 2);
        printf("Sender thread awake, checking for timeouts and new packets to send\n");
        for (int i = 0; i < N; i++) {
            Wait(semid, i);
            if (SM[i].isfree || !SM[i].isbound) {
                // if (i < 2) printf("k_socket %d is free or not bound, skipping\n", i);
                Signal(semid, i);
                continue;
            }
            window *W = &SM[i].swnd;
            time_t now = time(NULL);
            
            // Step 1: Check timeout for unacknowledged packets in swnd
            bool timeout = false;
            for (int j = W->start; j != W->end; j = (j + 1) % BUF_SIZE) {
                if (W->seq_num[j] != 0 && W->timestamp[j] != -1 && difftime(now, W->timestamp[j]) >= T) {
                    timeout = true;
                    break;
                }
            }

            // retransmit all packets in swnd
            if (timeout) {
                for (int j = W->start; j != W->end; j = (j + 1) % BUF_SIZE) {
                    if (W->seq_num[j] == 0 || W->timestamp[j] == -1) break;
                    printf("Timeout for packet with seq %d in buffer index %d for k_socket %d, retransmitting\n", W->seq_num[j], j, i);
                    retransmit_packet(i, j);
                }
            }

            // Step 2: Send new packets
            // update swnd size based on unacknowledged packets
            for (int idx = W->start; idx != W->end && W->size > 0; idx = (idx + 1) % BUF_SIZE) {
                if (SM[i].send_buf[idx][0] == '\0') {
                    printf("No new packet to send for k_socket %d\n", i);
                    break;
                }
                if (SM[i].swnd.timestamp[idx] != -1) {
                    printf("Packet with seq %d in buffer index %d for k_socket %d already sent, skipping\n", W->seq_num[idx], idx, i);
                    continue;
                }
                send_new_packet(i, idx);
                W->size--; // decrease swnd size for each new packet sent
                printf("Sent new packet with seq %d from buffer index %d for k_socket %d, msg : \n%s\n", W->last_seq, idx, i, SM[i].send_buf[idx]);
            }

            Signal(semid, i);
        }
    }
}

void *threadG() {
    printf("Garbage collector thread started\n");
    
    while (1) {
        sleep(T);
        for (int i = 0; i < N; i++) {
            Wait(semid, i);
            if (!SM[i].isfree) {
                int pid = SM[i].pid;
                // check if process still exists
                if (kill(pid, 0) == -1 && errno == ESRCH) {
                    Signal(semid, i);
                    // cleanup via k_close
                    k_close(i);
                    printf("Garbage collector closed socket %d\n", i);
                    continue;
                }
            }
            Signal(semid, i);
        }
    }
}

void cleanup(int signo) {
    // remove shared memory
    if (shmid != -1) {
        if (shmctl(shmid, IPC_RMID, NULL) == -1)
            perror("shmctl");
        else
            printf("Shared memory %d removed\n", shmid);
    }

    // remove semaphore set
    if (semid != -1) {
        if (semctl(semid, 0, IPC_RMID) == -1)
            perror("semctl");
        else
            printf("Semaphore set %d removed\n", semid);
    }

    if (signo == SIGSEGV)
        printf("Program terminated due to segmentation fault\n");
    else if (signo == SIGINT)
        printf("Program terminated (Ctrl+C)\n");

    if (signo != -1) exit(0);
    exit(1);
}

int main() {
    // create shared memory SM and semaphore sem
    key_t SHM_KEY = ftok(".", 'H');
    shmid = shmget(SHM_KEY, N * sizeof(sockinfo), IPC_CREAT | 0666);
    if (shmid >= 0) SM = (sockinfo*) shmat(shmid, NULL, 0);
    else {
        perror("shmget");
        cleanup(-1);
    }

    for (int i = 0; i < N; i++) {
        SM[i].isfree = true;
        SM[i].udpsockfd = -1;
        SM[i].pid = -1;
    }
    printf("Shared memory created with id %d\n", shmid);
    
    key_t SEM_KEY = ftok(".", 'E');
    semid = semget(SEM_KEY, N, IPC_CREAT | 0666);
    if (semid < 0) {
        perror("semget");
        cleanup(-1);
    }
    for (int i = 0; i < N; i++) {
        semctl(semid, i, SETVAL, 1); // initialize all semaphores to 1
    }
    printf("Semaphore set created with id %d\n", semid);
    
    signal(SIGINT, cleanup);
    signal(SIGSEGV, cleanup);

    // create 3 threads R, S and G
    pthread_t r_thread, s_thread, g_thread;
    pthread_create(&r_thread, NULL, threadR, NULL);
    pthread_create(&s_thread, NULL, threadS, NULL);
    pthread_create(&g_thread, NULL, threadG, NULL);

    pthread_exit(NULL);

    // while (1) pause(); // wait for signals

    return 0;
}