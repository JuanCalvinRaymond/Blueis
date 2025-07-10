#pragma once
#include <stddef.h>

struct DList
{
  DList *prev = NULL;
  DList *next = NULL;
};

inline void DListInit(DList *node)
{
  node->prev = node->next = node;
}

inline bool DListEmpty(DList *node)
{
  return node->next == node;
}

inline void DListDetach(DList *node)
{
  DList *prev = node->prev;
  DList *next = node->next;
  prev->next = next;
  next->prev = prev;
}

inline void DListInsertBefore(DList *target, DList *rookie)
{
  DList *prev = target->prev;
  prev->next = rookie;
  rookie->prev = prev;
  rookie->next = target;
  target->prev = rookie;
}