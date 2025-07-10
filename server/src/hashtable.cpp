#include <assert.h>
#include <stdlib.h>
#include "hashtable.h"

const size_t kRehashingWork = 128;
const size_t kMaxLoadFactor = 8;
static void init(HTab *htab, size_t n)
{
  assert(n > 0 && ((n - 1) & n) == 0);
  htab->bucket = (HNode **)calloc(n, sizeof(HNode *));
  htab->mask = n - 1;
  htab->size = 0;
}

static void insert(HTab *htab, HNode *node)
{
  size_t pos = node->hcode & htab->mask;
  // htab->bucket[pos] is pointer to the start of a table.
  HNode *next = htab->bucket[pos];
  node->next = next;
  htab->bucket[pos] = node;
  htab->size++;
}

static HNode **lookup(HTab *htab, HNode *key, bool(eq)(HNode *, HNode *))
{
  if (!htab->bucket)
  {
    return NULL;
  }

  size_t pos = key->hcode & htab->mask;
  HNode **from = &htab->bucket[pos];
  for (HNode *cur; (cur = *from) != NULL; from = &cur->next)
  {
    if (cur->hcode == key->hcode && eq(cur, key))
    {
      return from;
    }
  }
  return NULL;
}

static HNode *detach(HTab *htab, HNode **from)
{
  HNode *node = *from;
  *from = node->next;
  htab->size--;
  return node;
}

static bool forEach(HTab *htab, bool (*f)(HNode *, void *), void *arg)
{
  for (size_t i = 0; htab->mask && i <= htab->mask; i++)
  {
    for (HNode *node = htab->bucket[i]; node != NULL; node = node->next)
    {
      if (!f(node, arg))
        return false;
    }
  }
  return true;
}

static void HashMapHelpRehashing(HMap *hmap)
{
  size_t nwork = 0;
  while (nwork < kRehashingWork && hmap->older.size > 0)
  {
    HNode **from = &hmap->older.bucket[hmap->migrate_pos];
    if (!*from)
    {
      hmap->migrate_pos++;
      continue;
    }

    insert(&hmap->newer, detach(&hmap->older, from));
    nwork++;
  }

  if (hmap->older.size == 0 && hmap->older.bucket)
  {
    free(hmap->older.bucket);
    hmap->older = HTab{};
  }
}

static void HashMapTriggerRehashing(HMap *hmap)
{
  assert(hmap->older.bucket == NULL);

  hmap->older = hmap->newer;
  init(&hmap->newer, (hmap->newer.mask + 1) * 2);
  hmap->migrate_pos = 0;
}

HNode *HashMapLookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *))
{
  HashMapHelpRehashing(hmap);
  HNode **from = lookup(&hmap->newer, key, eq);
  if (!from)
  {
    from = lookup(&hmap->older, key, eq);
  }
  return from ? *from : NULL;
}

void HashMapInsert(HMap *hmap, HNode *node)
{
  if (!hmap->newer.bucket)
  {
    init(&hmap->newer, 4);
  }
  insert(&hmap->newer, node);

  if (!hmap->older.bucket)
  {
    size_t shreshold = (hmap->newer.mask + 1) * kMaxLoadFactor;
    if (hmap->newer.size >= shreshold)
    {
      HashMapTriggerRehashing(hmap);
    }
  }

  HashMapHelpRehashing(hmap);
}

HNode *HashMapDelete(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *))
{
  HashMapHelpRehashing(hmap);
  if (HNode **from = lookup(&hmap->newer, key, eq))
  {
    return detach(&hmap->newer, from);
  }
  if (HNode **from = lookup(&hmap->older, key, eq))
  {
    return detach(&hmap->older, from);
  }
  return NULL;
}

void HashMapClear(HMap *hmap)
{
  free(hmap->newer.bucket);
  free(hmap->older.bucket);
  *hmap = HMap{};
}

size_t HashMapSize(HMap *hmap)
{
  return hmap->newer.size + hmap->older.size;
}

void HashMapForEach(HMap *hmap, bool (*f)(HNode *, void *), void *arg)
{
  forEach(&hmap->newer, f, arg) && forEach(&hmap->older, f, arg);
}
