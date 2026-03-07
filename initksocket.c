#include "ksocket.h"

int main() {
    // create shared memory SM and semaphore sem
    int shmid = shmget(SHM_KEY, N * sizeof(int), IPC_CREAT | 0666);
    SM = (sockinfo*) shmat(shmid, NULL, 0);
    int semid = semget(SEM_KEY, N, IPC_CREAT | 0666);
    // create 3 threads R, S and G
}