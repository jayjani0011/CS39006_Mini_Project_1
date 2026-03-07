#include "ksocket.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>

void *threadR() {
    // receive messages from the UDP socket and add them to the receiver window of the corresponding socket in SM
    // need to use select() to check for incoming messages on all UDP sockets in SM
    fd_set readfds, masterfds;
    struct timeval timeout;

    while (1) {
        FD_ZERO(&readfds);
        int maxfd = -1;
        for (int i = 0; i < N; i++) {
            Wait(semid, i);
            if (!SM[i].isfree) {
                FD_SET(SM[i].udpsockfd, &readfds);
                if (SM[i].udpsockfd > maxfd) maxfd = SM[i].udpsockfd;
            }
            Signal(semid, i);
        }

        timeout.tv_sec = 1; // wait for 1 second for incoming messages
        timeout.tv_usec = 0;
        int activity = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
        if (activity < 0) {
            perror("select error");
            continue;
        }

        for (int i = 0; i < N; i++) {
            Wait(semid, i);
            if (!SM[i].isfree && FD_ISSET(SM[i].udpsockfd, &readfds)) {
                // receive the message and add it to the receiver window of the corresponding socket in SM
                char buf[MSG_SIZE];
                socklen_t addrlen = sizeof(SM[i].dest);
                ssize_t recvlen = recvfrom(SM[i].udpsockfd, buf, MSG_SIZE, 0, (struct sockaddr*) &SM[i].dest, &addrlen);
                if (recvlen < 0) {
                    perror("recvfrom error");
                    Signal(semid, i);
                    continue;
                }
                // check if the receiver window is full
                if ((SM[i].rwnd.start + 1) % BUF_SIZE == SM[i].rwnd.end) {
                    // drop this message and continue
                    Signal(semid, i);
                    continue; // receiver window is full
                }
                // add the message to the receiver window, removing the KTP header
                int idx = SM[i].rwnd.end;
                // check in the empty slots of the receiver window, if the sequence number matches the expected sequence number for that slot, if not drop the message, otherwise add it to the receiver window
                strncpy(SM[i].recv_buf[idx], buf + KTP_HEADER_SIZE, recvlen - KTP_HEADER_SIZE);
                SM[i].rwnd.end = (SM[i].rwnd.end + 1) % BUF_SIZE;
            }
            Signal(semid, i);
        }
    }
}

void *threadS() {
    // get messages from the sender window of each socket in SM and send them to the destination address through the corresponding UDP socket
//     The thread S behaves in the following manner. It sleeps for some time (T/2), and wakes up
// periodically. On waking up, it first checks whether the message timeout period (T) is over (by
// computing the time difference between the current time and the time when the messages
// within the window were sent last) for the messages sent over any of the active KTP sockets.
// If yes, it retransmits all the messages within the current swnd for that KTP socket. It then
// checks the current swnd for each of the KTP sockets and determines whether there is a
// pending message from the sender-side message buffer that can be sent. If so, it sends that
// message through the UDP sendto() call for the corresponding UDP socket and updates the
// send timestamp.

}

void *threadG() {
    // garbage collector thread to free up sockets that have not been closed properly by the user
}

int main() {
    // create shared memory SM and semaphore sem
    key_t SHM_KEY = ftok(".", 'H');
    shmid = shmget(SHM_KEY, N * sizeof(int), IPC_CREAT | 0666);
    SM = (sockinfo*) shmat(shmid, NULL, 0);
    for (int i = 0; i < N; i++) {
        SM[i].isfree = 1;
        SM[i].udpsockfd = -1;
        SM[i].pid = -1;
        SM[i].swnd.start = SM[i].swnd.end = 0;
        SM[i].rwnd.start = SM[i].rwnd.end = 0;
    }
    
    key_t SEM_KEY = ftok(".", 'E');
    semid = semget(SEM_KEY, N, IPC_CREAT | 0666);
    for (int i = 0; i < N; i++) {
        semctl(semid, i, SETVAL, 1); // initialize all semaphores to 1
    }

    // create 3 threads R, S and G
    pthread_t r_thread, s_thread, g_thread;
    pthread_create(&r_thread, NULL, threadR, NULL);
    pthread_create(&s_thread, NULL, threadS, NULL);
    pthread_create(&g_thread, NULL, threadG, NULL);

    return 0;
}