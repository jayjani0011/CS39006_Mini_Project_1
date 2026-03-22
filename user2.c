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

#include "ksocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *eof_marker = "~";

extern int semid, shmid;
extern sockinfo* SM;

int main(int argc, char *argv[]) {
    if (argc != 6) {
        printf("Usage: %s <src_ip> <src_port> <dest_ip> <dest_port> <output_file>\n", argv[0]);
        exit(1);
    }

    key_t SHM_KEY = ftok(".", 'H');
    shmid = shmget(SHM_KEY, N * sizeof(sockinfo), IPC_CREAT | 0666);
    if (shmid >= 0) SM = (sockinfo*) shmat(shmid, NULL, 0);
    else {
        perror("shmget");
        exit(1);
    }

    char *src_ip = strdup(argv[1]);
    int src_port = atoi(argv[2]);
    char *dest_ip = strdup(argv[3]);
    int dest_port = atoi(argv[4]);
    char *outfile = strdup(argv[5]);

    int sockfd = k_socket(AF_INET, SOCK_KTP, 0);
    if (sockfd < 0) {
        perror("k_socket");
        exit(1);
    }

    struct sockaddr_in src, dest;
    memset(&src, 0, sizeof(src));
    memset(&dest, 0, sizeof(dest));

    src.sin_family = AF_INET;
    src.sin_port = htons(src_port);
    src.sin_addr.s_addr = inet_addr(src_ip);

    dest.sin_family = AF_INET;
    dest.sin_port = htons(dest_port);
    dest.sin_addr.s_addr = inet_addr(dest_ip);

    if (k_bind(sockfd, (struct sockaddr *)&src, (struct sockaddr *)&dest) < 0) {
        perror("k_bind");
        exit(1);
    }

    FILE *fp = fopen(outfile, "wb");
    if (!fp) {
        perror("fopen");
        exit(1);
    }

    sleep(2); // wait for sender to be ready
    char buffer[MSG_SIZE + 1];
    while (1) {
        buffer[0] = '\0';
        ssize_t r = k_recvfrom(sockfd, buffer, MSG_SIZE, 0, NULL, NULL);
        if (r < 0) {
            if (errno == ENOMESSAGE || errno == EINVAL) {
                sleep(1); // wait and retry if no message to receive
                continue;
            }
            perror("k_recvfrom");
            exit(1);
        }
        buffer[r] = '\0'; // null terminate the received message

        if (memcmp(buffer, eof_marker, 1) == 0)
        {
            printf("user2: EOF marker received\n");
            break;
        }

        fwrite(buffer, 1, r, fp);
        fflush(fp);
        printf("Received %zu bytes\n", r);
        sleep(1); // simulate processing time
    }

    fclose(fp);
    k_close(sockfd);

    return 0;
}