#include "threadpool.h"
#include <assert.h>

static void *worker(void *arg)
{
  ThreadPool *pool = (ThreadPool *)arg;
  while (true)
  {
    pthread_mutex_lock(&pool->mu);
    while (pool->queue.empty())
    {
      pthread_cond_wait(&pool->not_empty, &pool->mu);
    }

    Work work = pool->queue.front();
    pool->queue.pop_front();
    pthread_mutex_unlock(&pool->mu);
    work.f(work.arg);
  }
  return NULL;
}

void ThreadPoolInit(ThreadPool *pool, size_t numThreads)
{
  assert(numThreads > 0);

  int rv = pthread_mutex_init(&pool->mu, NULL);
  assert(rv == 0);
  rv = pthread_cond_init(&pool->not_empty, NULL);
  assert(rv == 0);

  pool->threads.resize(numThreads);
  for (size_t i = 0; i < numThreads; i++)
  {
    rv = pthread_create(&pool->threads[i], NULL, &worker, pool);
    assert(rv == 0);
  }
}

void ThreadPoolQueue(ThreadPool *pool, void (*f)(void *), void *arg)
{
  pthread_mutex_lock(&pool->mu);
  pool->queue.push_back(Work{f, arg});
  pthread_cond_signal(&pool->not_empty);
  pthread_mutex_unlock(&pool->mu);
}
