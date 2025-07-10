#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "zset.h"
#include "common.h"

struct HKey
{
  HNode node;
  const char *name = NULL;
  size_t len = 0;
};

static bool hcmp(HNode *node, HNode *key)
{
  ZNode *znode = containerOf(node, ZNode, hmap);
  HKey *hkey = containerOf(key, HKey, node);
  if (znode->len != hkey->len)
  {
    return false;
  }
  return 0 == memcmp(znode->name, hkey->name, znode->len);
}

static ZNode *ZNodeNew(const char *name, size_t len, double score)
{
  ZNode *node = (ZNode *)malloc(sizeof(ZNode) + len);
  assert(node);
  avlInit(&node->tree);
  node->hmap.next = NULL;
  node->hmap.hcode = stringHash((uint8_t *)name, len);
  node->len = len;
  node->score = score;
  memcpy(&node->name[0], name, len);
  return node;
}

static void ZNodeDelete(ZNode *node)
{
  free(node);
}

static size_t min(size_t lhs, size_t rhs)
{
  return lhs < rhs ? lhs : rhs;
}

static bool ZLess(avlNode *lhs, double score, const char *name, size_t len)
{
  ZNode *zLeft = containerOf(lhs, ZNode, tree);
  if (zLeft->score != score)
  {
    return zLeft->score < score;
  }

  int rv = memcmp(zLeft->name, name, min(zLeft->len, len));
  if (rv != 0)
  {
    return rv < 0;
  }
  return zLeft->len < len;
}

static bool ZLess(avlNode *lhs, avlNode *rhs)
{
  ZNode *zRight = containerOf(rhs, ZNode, tree);
  return ZLess(lhs, zRight->score, zRight->name, zRight->len);
}

static void TreeInsert(ZSet *zset, ZNode *node)
{
  avlNode *parent = NULL;
  avlNode **from = &zset->root;
  while (*from)
  {
    parent = *from;
    from = ZLess(&node->tree, parent) ? &parent->left : &parent->right;
  }
  *from = &node->tree;
  node->tree.parent = parent;
  zset->root = avlFix(&node->tree);
}

static void TreeDispose(avlNode *node)
{
  if (!node)
  {
    return;
  }

  TreeDispose(node->left);
  TreeDispose(node->right);
  ZNodeDelete(containerOf(node, ZNode, tree));
}

static void ZSetUpdate(ZSet *zset, ZNode *node, double score)
{
  if (node->score == score)
  {
    return;
  }

  zset->root = avlDelete(&node->tree);
  avlInit(&node->tree);

  node->score = score;
  TreeInsert(zset, node);
}

bool ZSetInsert(ZSet *zset, const char *name, size_t len, double score)
{
  ZNode *node = ZSetLookup(zset, name, len);
  if (node)
  {
    ZSetUpdate(zset, node, score);
    return false;
  }
  else
  {
    node = ZNodeNew(name, len, score);
    HashMapInsert(&zset->hmap, &node->hmap);
    TreeInsert(zset, node);
    return true;
  }
}

ZNode *ZSetLookup(ZSet *zset, const char *name, size_t len)
{
  if (!zset->root)
  {
    return NULL;
  }

  HKey key;
  key.node.hcode = stringHash((uint8_t *)name, len);
  key.name = name;
  key.len = len;
  HNode *from = HashMapLookup(&zset->hmap, &key.node, &hcmp);
  return from ? containerOf(from, ZNode, hmap) : NULL;
}

void ZSetDelete(ZSet *zset, ZNode *node)
{
  HKey key;
  key.node.hcode = node->hmap.hcode;
  key.name = node->name;
  key.len = node->len;
  HNode *from = HashMapDelete(&zset->hmap, &key.node, &hcmp);
  assert(from);

  zset->root = avlDelete(&node->tree);

  ZNodeDelete(node);
}

ZNode *ZSetSeekge(ZSet *zset, double score, const char *name, size_t len)
{
  avlNode *from = NULL;

  for (avlNode *node = zset->root; node;)
  {
    if (ZLess(node, score, name, len))
    {
      node = node->right;
    }
    else
    {
      from = node;
      node = node->left;
    }
  }
  return from ? containerOf(from, ZNode, tree) : NULL;
}

ZNode *ZNodeOffset(ZNode *node, int64_t offset)
{
  avlNode *tnode = node ? avlOffset(&node->tree, offset) : NULL;
  return tnode ? containerOf(tnode, ZNode, tree) : NULL;
}

void ZSetClear(ZSet *zset)
{
  HashMapClear(&zset->hmap);
  TreeDispose(zset->root);
  zset->root = NULL;
}