#include "../include/client_queue.h"
#include <stdio.h>

client_queue_t* create_client_queue(int capacity) {
    if (capacity <= 0) {
        return NULL;
    }
    
    client_queue_t *queue = malloc(sizeof(client_queue_t));
    if (!queue) {
        return NULL;
    }
    
    queue->sockets = malloc(sizeof(int) * capacity);
    if (!queue->sockets) {
        free(queue);
        return NULL;
    }
    
    queue->capacity = capacity;
    queue->size = 0;
    queue->front = 0;
    queue->rear = -1;
    
    // Initialize synchronization primitives
    if (pthread_mutex_init(&queue->lock, NULL) != 0) {
        free(queue->sockets);
        free(queue);
        return NULL;
    }
    
    if (pthread_cond_init(&queue->not_empty, NULL) != 0 ||
        pthread_cond_init(&queue->not_full, NULL) != 0) {
        pthread_mutex_destroy(&queue->lock);
        free(queue->sockets);
        free(queue);
        return NULL;
    }
    
    return queue;
}

void destroy_client_queue(client_queue_t *queue) {
    if (!queue) return;
    
    pthread_mutex_destroy(&queue->lock);
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    free(queue->sockets);
    free(queue);
}

int enqueue_client(client_queue_t *queue, int socket) {
    if (!queue) return ERROR;
    
    pthread_mutex_lock(&queue->lock);
    
    // Wait while queue is full
    while (queue->size >= queue->capacity) {
        pthread_cond_wait(&queue->not_full, &queue->lock);
    }
    
    // Add socket to queue
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->sockets[queue->rear] = socket;
    queue->size++;
    
    // Signal that queue is not empty
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->lock);
    
    return SUCCESS;
}

int dequeue_client(client_queue_t *queue) {
    if (!queue) return ERROR;
    
    pthread_mutex_lock(&queue->lock);
    
    // Wait while queue is empty
    while (queue->size <= 0) {
        pthread_cond_wait(&queue->not_empty, &queue->lock);
    }
    
    // Remove socket from queue
    int socket = queue->sockets[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size--;
    
    // Signal that queue is not full
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->lock);
    
    return socket;
}
