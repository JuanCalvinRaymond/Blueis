#pragma once

#include <stddef.h>
#include <cstdint>

struct avlNode
{
  avlNode *parent = NULL;
  avlNode *left = NULL;
  avlNode *right = NULL;
  uint32_t height = 0;
  uint32_t count = 0;
};

inline void avlInit(avlNode *node)
{
  node->left = node->right = node->parent = NULL;
  node->height = 1;
  node->count = 1;
}

inline uint32_t avlHeight(avlNode *node) { return node ? node->height : 0; }
inline uint32_t avlCount(avlNode *node) { return node ? node->count : 0; }

avlNode *avlFix(avlNode *node);
avlNode *avlDelete(avlNode *node);
avlNode *avlOffset(avlNode *node, int64_t offset);