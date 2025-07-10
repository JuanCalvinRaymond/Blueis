#pragma once
#include <stddef.h>
#include <stdint.h>

struct HNode
{
  HNode *next = NULL;
  uint64_t hcode = 0;
};

struct HTab
{
  HNode **bucket = NULL;
  size_t mask = 0;
  size_t size = 0;
};

struct HMap
{
  HTab newer;
  HTab older;
  size_t migrate_pos = 0;
};

HNode *HashMapLookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void HashMapInsert(HMap *hmap, HNode *key);
HNode *HashMapDelete(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *));
void HashMapClear(HMap *hmap);
size_t HashMapSize(HMap *hmap);

void HashMapForEach(HMap *hmap, bool (*f)(HNode *, void *), void *arg);