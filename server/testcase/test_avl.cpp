#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <set>
#include "avl.h"

#define containerOf(ptr, type, member) ({\
  const typeof(((type*)0)->member) * __mptr = (ptr);\
  (type*)((char*)__mptr - offsetof(type, member)); })

struct Data
{
  avlNode node;
  uint32_t val = 0;
};

struct Container
{
  avlNode *root = NULL;
};

static void add(Container &container, uint32_t val)
{
  Data *data = new Data();
  avlInit(&data->node);
  data->val = val;

  avlNode *cur = NULL;
  avlNode **from = &container.root;
  while (*from)
  {
    cur = *from;
    uint32_t nodeVal = containerOf(cur, Data, node)->val;
    from = (val < nodeVal) ? &cur->left : &cur->right;
  }

  *from = &data->node;
  data->node.parent = cur;
  container.root = avlFix(&data->node);
}

static bool del(Container &container, uint32_t val)
{
  avlNode *cur = container.root;
  while (cur)
  {
    uint32_t nodeVal = containerOf(cur, Data, node)->val;
    if (val == nodeVal)
    {
      break;
    }
    cur = val < nodeVal ? cur->left : cur->right;
  }

  if (!cur)
  {
    return false;
  }

  container.root = avlDelete(cur);
  delete containerOf(cur, Data, node);
  return true;
}

static void avlVerify(avlNode *parent, avlNode *node)
{
  if (!node)
  {
    return;
  }

  assert(node->parent == parent);
  avlVerify(node, node->left);
  avlVerify(node, node->right);

  assert(node->count == 1 + avlCount(node->left) + avlCount(node->right));

  uint32_t leftHeight = avlHeight(node->left);
  uint32_t rightHeight = avlHeight(node->right);
  assert(node->height == 1 + std::max(leftHeight, rightHeight));

  uint32_t val = containerOf(node, Data, node)->val;
  if (node->left)
  {
    assert(node->left->parent == node);
    assert(containerOf(node->left, Data, node)->val <= val);
  }
  if (node->right)
  {
    assert(node->right->parent == node);
    assert(containerOf(node->right, Data, node)->val >= val);
  }
}

static void extract(avlNode *node, std::multiset<uint32_t> &extracted)
{
  if (!node)
  {
    return;
  }
  extract(node->left, extracted);
  extracted.insert(containerOf(node, Data, node)->val);
  extract(node->right, extracted);
}

static void containerVerify(Container &container, const std::multiset<uint32_t> &ref)
{
  avlVerify(NULL, container.root);
  assert(avlCount(container.root) == ref.size());
  std::multiset<uint32_t> extracted;
  extract(container.root, extracted);
  assert(extracted == ref);
  printf("reference: %lu\n", ref.size());
}

static void dispose(Container &container)
{
  while (container.root)
  {
    avlNode *node = container.root;
    container.root = avlDelete(container.root);
    delete containerOf(node, Data, node);
  }
}

static void testInsert(uint32_t size)
{
  for (uint32_t val = 0; val < size; val++)
  {
    Container container;
    std::multiset<uint32_t> ref;
    for (uint32_t i = 0; i < size; i++)
    {
      if (i == val)
      {
        continue;
      }
      add(container, i);
      ref.insert(i);
    }
    containerVerify(container, ref);

    add(container, val);
    ref.insert(val);
    containerVerify(container, ref);
    dispose(container);
  }
}

static void testInsertDuplicate(uint32_t size)
{
  for (uint32_t val = 0; val < size; val++)
  {
    Container container;
    std::multiset<uint32_t> ref;
    for (uint32_t i = 0; i < size; i++)
    {
      add(container, i);
      ref.insert(i);
    }
    containerVerify(container, ref);

    add(container, val);
    ref.insert(val);
    containerVerify(container, ref);
    dispose(container);
  }
}

static void testRemove(uint32_t size)
{
  for (uint32_t val = 0; val < size; val++)
  {
    Container container;
    std::multiset<uint32_t> ref;
    for (uint32_t i = 0; i < size; i++)
    {
      add(container, i);
      ref.insert(i);
    }
    containerVerify(container, ref);

    assert(del(container, val));
    ref.erase(val);
    containerVerify(container, ref);
    dispose(container);
  }
}

int main()
{
  Container container;

  containerVerify(container, {});
  add(container, 123);
  containerVerify(container, {123});
  assert(!del(container, 124));
  assert(del(container, 123));
  containerVerify(container, {});

  std::multiset<uint32_t> ref;
  for (uint32_t i = 0; i < 1000; i += 3)
  {
    add(container, i);
    ref.insert(i);
    printf("Index: %d", i);
    containerVerify(container, ref);
  }

  for (uint32_t i = 0; i < 100; i++)
  {
    uint32_t val = (uint32_t)rand() % 1000;
    add(container, val);
    ref.insert(val);
    printf("Index random: %d", i);
    containerVerify(container, ref);
  }

  for (uint32_t i = 0; i < 200; i++)
  {
    uint32_t val = (uint32_t)rand() % 1000;
    auto it = ref.find(val);
    if (it == ref.end())
    {
      printf("Fail deleting: %d", i);
      assert(!del(container, val));
    }
    else
    {
      printf("Success deleting: %d", i);
      assert(del(container, val));
      ref.erase(it);
    }
    printf("Index delete random: %d", i);
    containerVerify(container, ref);
  }

  for (uint32_t i = 0; i < 200; i++)
  {
    testInsert(i);
    testInsertDuplicate(i);
    testRemove(i);
  }

  dispose(container);
  return 0;
}
