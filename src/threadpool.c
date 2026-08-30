#include "../include/threadpool.h"
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

// Queue methods.
//
// The queue is a fixed-capacity ring buffer. It carries no lock of its own:
// every call below runs with the owning pool's single mutex held, so the
// queue state (front/rear/count) and the pool's condition-variable predicates
// stay coherent under one lock. queue_push must only be called when there is
// a free slot (count < size); the pool enforces that via slot_available.

void queue_init(queue_t *queue, int size)
{
    queue->tasks = (task_t *)malloc(sizeof(task_t) * size);
    queue->front = 0;
    queue->rear = -1;
    queue->count = 0;
    queue->size = size;
}

void queue_push(queue_t *queue, task_t task)
{
    queue->rear = (queue->rear + 1) % queue->size;
    queue->tasks[queue->rear] = task;
    queue->count++;
}

task_t queue_pop(queue_t *queue)
{
    task_t task = queue->tasks[queue->front];
    queue->front = (queue->front + 1) % queue->size;
    queue->count--;
    return task;
}

// Thread pool methods.

void *thread_pool_worker(void *arg)
{
    thread_pool_t *pool = (thread_pool_t *)arg;
    while (1)
    {
        pthread_mutex_lock(&pool->lock);
        while (!pool->shutdown && pool->queue.count == 0)
        {
            pthread_cond_wait(&pool->tasks_available, &pool->lock);
        }

        // Drain remaining work even while shutting down; only exit once the
        // queue is empty.
        if (pool->shutdown && pool->queue.count == 0)
        {
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        task_t task = queue_pop(&pool->queue);
        // A slot just opened up; a blocked producer may proceed.
        pthread_cond_signal(&pool->slot_available);
        pthread_mutex_unlock(&pool->lock);

        task.func(task.arg);

        pthread_mutex_lock(&pool->lock);
        pool->active_tasks--;
        if (pool->active_tasks == 0)
        {
            pthread_cond_signal(&pool->all_tasks_done);
        }
        pthread_mutex_unlock(&pool->lock);
    }
    return NULL;
}

thread_pool_t *thread_pool_create(int num_threads, int queue_size)
{
    thread_pool_t *pool = (thread_pool_t *)malloc(sizeof(thread_pool_t));
    pthread_cond_init(&pool->all_tasks_done, NULL);
    pthread_cond_init(&pool->tasks_available, NULL);
    pthread_cond_init(&pool->slot_available, NULL);
    pthread_mutex_init(&pool->lock, NULL);
    pool->num_threads = num_threads;
    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * num_threads);
    queue_init(&pool->queue, queue_size);
    pool->shutdown = 0;     // Not shutting down yet
    pool->active_tasks = 0; // No outstanding work yet
    for (int i = 0; i < num_threads; i++)
    {
        pthread_create(&pool->threads[i], NULL, thread_pool_worker, (void *)pool);
    }
    return pool;
}

void thread_pool_add_task(thread_pool_t *pool, void (*func)(void *), void *arg)
{
    pthread_mutex_lock(&pool->lock);

    // Bounded queue: block until a slot is free rather than overwriting
    // an unprocessed task (which would leak its client fd). If the pool is
    // shutting down, drop the task instead of enqueuing.
    while (pool->queue.count == pool->queue.size && !pool->shutdown)
    {
        pthread_cond_wait(&pool->slot_available, &pool->lock);
    }
    if (pool->shutdown)
    {
        pthread_mutex_unlock(&pool->lock);
        return;
    }

    pool->active_tasks++;

    task_t task;
    task.func = func;
    task.arg = arg;
    queue_push(&pool->queue, task);

    pthread_cond_signal(&pool->tasks_available);
    pthread_mutex_unlock(&pool->lock);
}

void thread_pool_cleanup(thread_pool_t *pool)
{
    pthread_mutex_lock(&pool->lock);

    // Wait for all submitted work to finish before signalling shutdown.
    while (pool->active_tasks > 0)
    {
        pthread_cond_wait(&pool->all_tasks_done, &pool->lock);
    }

    pool->shutdown = 1;
    // Wake every worker (draining an empty queue) and any blocked producer.
    pthread_cond_broadcast(&pool->tasks_available);
    pthread_cond_broadcast(&pool->slot_available);
    pthread_mutex_unlock(&pool->lock);

    for (int i = 0; i < pool->num_threads; i++)
    {
        pthread_join(pool->threads[i], NULL);
    }

    free(pool->threads);
    free(pool->queue.tasks);
    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->all_tasks_done);
    pthread_cond_destroy(&pool->tasks_available);
    pthread_cond_destroy(&pool->slot_available);
    free(pool);
}
