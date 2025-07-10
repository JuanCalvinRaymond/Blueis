#include "avl.h"
#include <assert.h>

static uint32_t max(uint32_t lhs, uint32_t rhs)
{
  return lhs < rhs ? rhs : lhs;
}

static void avlUpdate(avlNode *node)
{
  node->height = 1 + max(avlHeight(node->left), avlHeight(node->right));
  node->count = 1 + avlCount(node->left) + avlCount(node->right);
}

static avlNode *rotateLeft(avlNode *node)
{
  avlNode *parent = node->parent;
  avlNode *newNode = node->right;
  avlNode *inner = newNode->left;

  node->right = inner;
  if (inner)
  {
    inner->parent = node;
  }

  newNode->parent = parent;

  newNode->left = node;
  node->parent = newNode;

  avlUpdate(node);
  avlUpdate(newNode);
  return newNode;
}

static avlNode *rotateRight(avlNode *node)
{
  avlNode *parent = node->parent;
  avlNode *newNode = node->left;
  avlNode *inner = newNode->right;

  node->left = inner;
  if (inner)
  {
    inner->parent = node;
  }

  newNode->parent = parent;

  newNode->right = node;
  node->parent = newNode;

  avlUpdate(node);
  avlUpdate(newNode);
  return newNode;
}

static avlNode *avlFixLeft(avlNode *node)
{
  if (avlHeight(node->left->left) < avlHeight(node->left->right))
  {
    node->left = rotateLeft(node->left);
  }
  return rotateRight(node);
}

static avlNode *avlFixRight(avlNode *node)
{
  if (avlHeight(node->right->right) < avlHeight(node->right->left))
  {
    node->right = rotateRight(node->right);
  }
  return rotateLeft(node);
}

avlNode *avlFix(avlNode *node)
{
  while (1)
  {
    avlNode **from = &node;
    avlNode *parent = node->parent;
    if (parent)
    {
      from = parent->left == node ? &parent->left : &parent->right;
    }

    avlUpdate(node);

    uint32_t leftHeight = avlHeight(node->left);
    uint32_t rightHeight = avlHeight(node->right);

    if (leftHeight == rightHeight + 2)
    {
      *from = avlFixLeft(node);
    }
    else if (leftHeight + 2 == rightHeight)
    {
      *from = avlFixRight(node);
    }

    if (!parent)
    {
      return *from;
    }

    node = parent;
  }
}

static avlNode *avlDeleteEasy(avlNode *node)
{
  assert(!node->left || !node->right);
  avlNode *child = node->left ? node->left : node->right;
  avlNode *parent = node->parent;
  if (child)
  {
    child->parent = parent;
  }
  if (!parent)
  {
    return child;
  }

  avlNode **from = parent->left == node ? &parent->left : &parent->right;
  *from = child;
  return avlFix(parent);
}

avlNode *avlDelete(avlNode *node)
{
  if (!node->left || !node->right)
  {
    return avlDeleteEasy(node);
  }

  avlNode *victim = node->right;
  while (victim->left)
  {
    victim = victim->left;
  }

  avlNode *root = avlDeleteEasy(victim);
  *victim = *node;
  if (victim->left)
  {
    victim->left->parent = victim;
  }
  if (victim->right)
  {
    victim->right->parent = victim;
  }

  avlNode **from = &root;
  avlNode *parent = node->parent;
  if (parent)
  {
    from = parent->left == node ? &parent->left : &parent->right;
  }

  *from = victim;
  return root;
}

avlNode *avlOffset(avlNode *node, int64_t offset)
{
  int64_t pos = 0;

  while (offset != pos)
  {
    if (pos < offset && pos + avlCount(node->right) >= offset)
    {
      node = node->right;
      pos += avlCount(node->left) + 1;
    }
    else if (pos > offset && pos - avlCount(node->left) <= offset)
    {
      node = node->left;
      pos -= avlCount(node->right) + 1;
    }
    else
    {
      avlNode *parent = node->parent;
      if (!parent)
      {
        return NULL;
      }
      if (parent->right == node)
      {
        pos -= avlCount(node->left) + 1;
      }
      else
      {
        pos += avlCount(node->right) + 1;
      }
      node = parent;
    }
  }
  return node;
}
