#include "../include/threadpool.h"
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

// Queue methods.

queue_t *queue_init(int size)
{
    queue_t *queue = (queue_t *)malloc(sizeof(queue_t));
    queue->tasks = (task_t *)malloc(sizeof(task_t) * size);
    queue->front = 0;
    queue->rear = -1;
    queue->count = 0;
    queue->size = size;
    pthread_mutex_init(&queue->lock, NULL);
    return queue;
}

void queue_push(queue_t *queue, task_t task)
{
    pthread_mutex_lock(&queue->lock);
    queue->rear = (queue->rear + 1) % queue->size;
    queue->tasks[queue->rear] = task;
    queue->count++;
    pthread_mutex_unlock(&queue->lock);
}

task_t queue_pop(queue_t *queue)
{
    pthread_mutex_lock(&queue->lock);
    task_t task = queue->tasks[queue->front];
    queue->front = (queue->front + 1) % queue->size;
    queue->count--;
    pthread_mutex_unlock(&queue->lock);
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

        // recheck conditions after waking up
        if (pool->shutdown && pool->queue.count == 0)
        {
            pthread_mutex_unlock(&pool->lock);
            break;
        }

        task_t task = queue_pop(&pool->queue);
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
    pthread_mutex_init(&pool->lock, NULL);
    pool->num_threads = num_threads;
    pool->threads = (pthread_t *)malloc(sizeof(pthread_t) * num_threads);
    pool->queue = *queue_init(queue_size);
    pool->shutdown = 0; // Not shutting down yet
    for (int i = 0; i < num_threads; i++)
    {
        pthread_create(&pool->threads[i], NULL, thread_pool_worker, (void *)pool);
    }
    return pool;
}

void thread_pool_add_task(thread_pool_t *pool, void (*func)(void *), void *arg)
{
    pthread_mutex_lock(&pool->lock);
    pool->active_tasks++;
    pthread_mutex_unlock(&pool->lock);

    task_t task;
    task.func = func;
    task.arg = arg;
    queue_push(&pool->queue, task);
    pthread_cond_signal(&pool->tasks_available);
}

void thread_pool_cleanup(thread_pool_t *pool)
{
    pthread_mutex_lock(&pool->lock);
    if (pool->active_tasks > 0)
    {
        while (pool->active_tasks > 0)
        {
            pthread_cond_wait(&pool->all_tasks_done, &pool->lock);
        }
    }
    pthread_cond_broadcast(&pool->tasks_available);
    pool->shutdown = 1;
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
    free(pool);
}