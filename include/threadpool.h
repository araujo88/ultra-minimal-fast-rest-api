#ifndef _THREADPOOL_H
#define _THREADPOOL_H 1

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

// Define a task.
typedef struct
{
    void (*func)(void *);
    void *arg;
} task_t;

// Define the queue structure. All access is serialized by the owning
// pool's single mutex; the queue holds no lock of its own.
typedef struct
{
    task_t *tasks;
    int front;
    int rear;
    int count;
    int size;
} queue_t;

// Define the thread pool.
typedef struct
{
    pthread_t *threads;
    int num_threads;
    queue_t queue;
    int shutdown;
    int active_tasks;
    pthread_mutex_t lock;
    pthread_cond_t all_tasks_done;
    pthread_cond_t tasks_available; // signalled when the queue becomes non-empty
    pthread_cond_t slot_available;  // signalled when the queue becomes non-full
} thread_pool_t;

// Queue methods. The caller MUST hold pool->lock; these do not lock.

void queue_init(queue_t *queue, int size);
void queue_push(queue_t *queue, task_t task);
task_t queue_pop(queue_t *queue);

// Thread pool methods.

void *thread_pool_worker(void *arg);
thread_pool_t *thread_pool_create(int num_threads, int queue_size);
void thread_pool_add_task(thread_pool_t *pool, void (*func)(void *), void *arg);
void thread_pool_cleanup(thread_pool_t *pool);

#endif