#pragma once

#include <stddef.h>
#include <pthread.h>
#include <vector>
#include <deque>

struct Work
{
  void (*f)(void *) = NULL;
  void *arg = NULL;
};

struct ThreadPool
{
  std::vector<pthread_t> threads;
  std::deque<Work> queue;
  pthread_mutex_t mu;
  pthread_cond_t not_empty;
};

void ThreadPoolInit(ThreadPool *pool, size_t numThreads);
void ThreadPoolQueue(ThreadPool *pool, void (*f)(void *), void *arg);