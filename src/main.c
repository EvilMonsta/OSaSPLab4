#include <stdio.h>
#include <stdlib.h>
#include <stdint.h> 
#include <string.h> //
#include <unistd.h>
#include <sys/ipc.h> //
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/types.h> //
#include <sys/wait.h>
#include <signal.h> //
#include <time.h>

#define QUEUE_CAPACITY 10
#define DATA_MAX 256
#define SHM_KEY 0x1234
#define SEM_KEY 0x5678
#define MAX_PROCESSES 64

volatile sig_atomic_t stop = 0;

void handle_exit(int sig) {
    stop = 1;
}

struct Message {
    uint8_t type;
    uint16_t hash;
    uint8_t size;
    uint8_t data[DATA_MAX];
};

struct MessageQueue {
    struct Message buffer[QUEUE_CAPACITY];
    int head;
    int tail;
    int count;
    int added_total;
    int consumed_total;
};

union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};

enum { SEM_MUTEX = 0, SEM_EMPTY, SEM_FULL };

int sem_wait_op(int semid, int semnum) {
    struct sembuf op = { semnum, -1, 0 };
    return semop(semid, &op, 1);
}

int sem_signal_op(int semid, int semnum) {
    struct sembuf op = { semnum, 1, 0 };
    return semop(semid, &op, 1);
}

uint16_t calculate_hash(uint8_t type, uint8_t size, uint8_t* data) {
    uint16_t hash = 0;
    hash += type;
    for (int i = 0; i < size; i++) {
        hash += data[i];
    }
    return hash;
}

void producer(struct MessageQueue* q, int semid) {
    unsigned int seed = getpid();
    signal(SIGINT, handle_exit);

    while (!stop) {
        sleep(5);
        sem_wait_op(semid, SEM_EMPTY);
        sem_wait_op(semid, SEM_MUTEX);

        struct Message* msg = &q->buffer[q->head];
        msg->type = rand_r(&seed) % 256;
        msg->size = (rand_r(&seed) % 256) + 1;
        int padded = ((msg->size + 3) / 4) * 4;
        for (int i = 0; i < padded; i++) msg->data[i] = rand_r(&seed) % 256;
        msg->hash = 0;
        msg->hash = calculate_hash(msg->type, msg->size, msg->data);

        q->head = (q->head + 1) % QUEUE_CAPACITY;
        q->count++;
        q->added_total++;

        sem_signal_op(semid, SEM_MUTEX);
        sem_signal_op(semid, SEM_FULL);

        printf("[Producer %d] Added message #%d (size=%d)\n", getpid(), q->added_total, msg->size);
        fflush(stdout);
    }

    exit(0);
}

void consumer(struct MessageQueue* q, int semid) {
    signal(SIGINT, handle_exit);

    while (!stop) {
        sleep(5);
        sem_wait_op(semid, SEM_FULL);
        sem_wait_op(semid, SEM_MUTEX);

        struct Message* msg = &q->buffer[q->tail];
        uint16_t calc = calculate_hash(msg->type, msg->size, msg->data);
        int id = ++q->consumed_total;
        q->tail = (q->tail + 1) % QUEUE_CAPACITY;
        q->count--;

        sem_signal_op(semid, SEM_MUTEX);
        sem_signal_op(semid, SEM_EMPTY);

        printf("[Consumer %d] Consumed message #%d, hash %s\n", getpid(), id, (calc == msg->hash) ? "OK" : "FAIL");
        fflush(stdout);
    }

    exit(0);
}

void print_state(struct MessageQueue* q, int prod_count, int cons_count) {
    printf("\n=== Queue State ===\n");
    printf("Capacity:  %d\n", QUEUE_CAPACITY);
    printf("Used:      %d\n", q->count);
    printf("Free:      %d\n", QUEUE_CAPACITY - q->count);
    printf("Added:     %d\n", q->added_total);
    printf("Consumed:  %d\n", q->consumed_total);
    printf("Producers: %d\n", prod_count);
    printf("Consumers: %d\n", cons_count);
    printf("===================\n\n");
    fflush(stdout);
}

void kill_last(pid_t* list, int* count, const char* type) {
    if (*count > 0) {
        pid_t pid = list[*count - 1];
        if (kill(pid, SIGTERM) == 0) {  
            waitpid(pid, NULL, 0);
            (*count)--;
            printf("Killed last %s process (%d)\n", type, pid);
        } else {
            perror("kill");
        }
    } else {
        printf("No %s processes to kill.\n", type);
    }
}

int main() {
    signal(SIGINT, handle_exit);

    int shmid = shmget(SHM_KEY, sizeof(struct MessageQueue), IPC_CREAT | 0666);
    struct MessageQueue* q = (struct MessageQueue*)shmat(shmid, NULL, 0);
    memset(q, 0, sizeof(*q));

    int semid = semget(SEM_KEY, 3, IPC_CREAT | 0666);
    union semun arg;
    arg.val = 1; semctl(semid, SEM_MUTEX, SETVAL, arg);
    arg.val = QUEUE_CAPACITY; semctl(semid, SEM_EMPTY, SETVAL, arg);
    arg.val = 0; semctl(semid, SEM_FULL, SETVAL, arg);

    printf("Commands:\n'p' - new producer\n'c' - new consumer\n'k' - kill last process\n's' - show state\n'q' - quit\n");

    pid_t producers[MAX_PROCESSES];
    pid_t consumers[MAX_PROCESSES];
    int prod_count = 0, cons_count = 0;

    while (!stop) {
        char cmd = getchar();
        if (cmd == 'p') {
            if (prod_count < MAX_PROCESSES) {
                pid_t pid = fork();
                if (pid == 0) producer(q, semid);
                else producers[prod_count++] = pid;
            }
        }
        else if (cmd == 'c') {
            if (cons_count < MAX_PROCESSES) {
                pid_t pid = fork();
                if (pid == 0) consumer(q, semid);
                else consumers[cons_count++] = pid;
            }
        }
        else if (cmd == 's') {
            print_state(q, prod_count, cons_count);
        }
        else if (cmd == 'k') {
            printf("Kill [p]roducer or [c]onsumer? ");
            char sub;
            scanf(" %c", &sub);
            if (sub == 'p') kill_last(producers, &prod_count, "producer");
            else if (sub == 'c') kill_last(consumers, &cons_count, "consumer");
        }
        else if (cmd == 'q') {
            break;
        }
    }

    for (int i = 0; i < prod_count; i++) kill(producers[i], SIGINT);
    for (int i = 0; i < cons_count; i++) kill(consumers[i], SIGINT);
    while (wait(NULL) > 0);

    semctl(semid, 0, IPC_RMID);
    shmctl(shmid, IPC_RMID, NULL);
    printf("Exit.\n");
    return 0;
}