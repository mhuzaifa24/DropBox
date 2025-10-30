#ifndef CLIENT_QUEUE_H
#define CLIENT_QUEUE_H

#include "common.h"

typedef struct {
    int *sockets;
    int capacity;
    int size;
    int front;
    int rear;
    pthread_mutex_t lock;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} client_queue_t;

client_queue_t* create_client_queue(int capacity);
void destroy_client_queue(client_queue_t *queue);
int enqueue_client(client_queue_t *queue, int socket);
int dequeue_client(client_queue_t *queue);

#endif
