#include "heap.h"

static size_t HeapParent(size_t i)
{
  return (i + 1) / 2 - 1;
}

static size_t HeapLeft(size_t i)
{
  return (i * 2) + 1;
}

static size_t HeapRight(size_t i)
{
  return (i * 2) + 2;
}

static void HeapUp(HeapItem *heapItem, size_t pos)
{
  HeapItem temp = heapItem[pos];
  while (pos > 0 && heapItem[HeapParent(pos)].val > temp.val)
  {
    heapItem[pos] = heapItem[HeapParent(pos)];
    *heapItem[pos].ref = pos;
    pos = HeapParent(pos);
  }
  heapItem[pos] = temp;
  *heapItem[pos].ref = pos;
}

static void HeapDown(HeapItem *heapItem, size_t pos, size_t len)
{
  HeapItem temp = heapItem[pos];
  while (true)
  {
    size_t left = HeapLeft(pos);
    size_t right = HeapRight(pos);
    size_t minPos = pos;
    uint64_t minVal = temp.val;
    if (left < len && heapItem[left].val < minVal)
    {
      minPos = left;
      minVal = heapItem[left].val;
    }
    if (right < len && heapItem[right].val < minVal)
    {
      minPos = right;
    }
    if (minPos == pos)
    {
      break;
    }
    heapItem[pos] = heapItem[minPos];
    *heapItem[pos].ref = pos;
    pos = minPos;
  }

  heapItem[pos] = temp;
  *heapItem[pos].ref = pos;
}

void HeapUpdate(HeapItem *heapItem, size_t pos, size_t len)
{
  if (pos > 0 && heapItem[HeapParent(pos)].val > heapItem[pos].val)
  {
    HeapUp(heapItem, pos);
  }
  else
  {
    HeapDown(heapItem, pos, len);
  }
}