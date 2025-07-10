#ifndef COMMON_H
#define COMMON_H
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <unistd.h>

#define containerOf(ptr, type, member) ({ \
  const typeof(((type*)0)->member) *__mptr = (ptr); \
  (type*) ((char*)__mptr - offsetof(type, member)); })

static uint64_t stringHash(const uint8_t *data, size_t len)
{
  uint32_t h = 0x811C9DC5;
  for (size_t i = 0; i < len; i++)
  {
    h = (h + data[i]) * 0x01000193;
  }
  return h;
}

#endif