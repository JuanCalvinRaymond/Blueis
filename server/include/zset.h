#pragma once
#include "avl.h"
#include "hashtable.h"

struct ZSet
{
  avlNode *root = NULL;
  HMap hmap;
};

struct ZNode
{
  avlNode tree;
  HNode hmap;
  double score = 0;
  size_t len = 0;
  char name[0];
};

bool ZSetInsert(ZSet *zset, const char *name, size_t len, double score);
ZNode *ZSetLookup(ZSet *zset, const char *name, size_t len);
void ZSetDelete(ZSet *zset, ZNode *node);
ZNode *ZSetSeekge(ZSet *zset, double score, const char *name, size_t len);
void ZSetClear(ZSet *zset);
ZNode *ZNodeOffset(ZNode *node, int64_t offset);