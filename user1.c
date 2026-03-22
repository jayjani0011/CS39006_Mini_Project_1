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
        printf("Usage: %s <src_ip> <src_port> <dest_ip> <dest_port> <file>\n", argv[0]);
        exit(1);
    }

    key_t SHM_KEY = ftok(".", 'H');
    shmid = shmget(SHM_KEY, N * sizeof(sockinfo), IPC_CREAT | 0666);
    printf("SHMID : %d\n", shmid);

    if (shmid >= 0) {
        SM = (sockinfo*) shmat(shmid, NULL, 0);
    }
    else {
        perror("shmget");
        exit(1);
    }

    char *src_ip = strdup(argv[1]);
    int src_port = atoi(argv[2]);
    char *dest_ip = strdup(argv[3]);
    int dest_port = atoi(argv[4]);
    char *filename = strdup(argv[5]);

    printf("Source IP: %s, Source Port: %d\n", src_ip, src_port);
    printf("Destination IP: %s, Destination Port: %d\n", dest_ip, dest_port);
    printf("File to send: %s\n", filename);
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

    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("fopen");
        exit(1);
    }

    sleep(2); // wait for receiver to be ready
    char buffer[MSG_SIZE + 1];
    bool done = false;

    while (1) {
        buffer[0] = '\0';
        size_t bytes = fread(buffer, 1, MSG_SIZE, fp);
        buffer[bytes] = '\0';

        if (bytes == 0) {
            done = true;
            printf("user1: End of file reached.\n");
            strcpy(buffer, eof_marker);
            bytes = 1;
        }

        while (1) {
            int ret = k_sendto(sockfd, buffer, bytes, 0, (struct sockaddr *)&dest, sizeof(dest));
            int err = errno;
            if (ret >= 0) break;
            // printf("k_sendto error: %d\n", err);
            if (err != ENOSPACE && err != ENOTBOUND && err != EINVAL) {
                perror("k_sendto");
                exit(1);
            }
            // sender window full — wait a bit so ACK thread can slide window
            sleep(1);
        }
        
        printf("Sent %zu bytes\n", bytes);
        sleep(1); // simulate processing time

        if(done) {
            printf("user1: EOF marker sent.\n");
            break;
        }
    }

    printf("File sent successfully\n");
    fclose(fp);

    sleep(100); // this will prevent threadS to ignore this sockfd
    k_close(sockfd);

    return 0;
}