#include <assert.h>
#include "avl.h"

#define container_of(ptr, type, member) ({                  \
    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
    (type *)( (char *)__mptr - offsetof(type, member) ); })

struct Data
{
  avlNode node;
  uint32_t val = 0;
};

struct Container
{
  avlNode *root = NULL;
};

static void add(Container &c, uint32_t val)
{
  Data *data = new Data();
  avlInit(&data->node);
  data->val = val;

  if (!c.root)
  {
    c.root = &data->node;
    return;
  }

  avlNode *cur = c.root;
  while (true)
  {
    avlNode **from =
        (val < container_of(cur, Data, node)->val)
            ? &cur->left
            : &cur->right;
    if (!*from)
    {
      *from = &data->node;
      data->node.parent = cur;
      c.root = avlFix(&data->node);
      break;
    }
    cur = *from;
  }
}

static void dispose(avlNode *node)
{
  if (node)
  {
    dispose(node->left);
    dispose(node->right);
    delete container_of(node, Data, node);
  }
}

static void test_case(uint32_t sz)
{
  Container c;
  for (uint32_t i = 0; i < sz; ++i)
  {
    add(c, i);
  }

  avlNode *min = c.root;
  while (min->left)
  {
    min = min->left;
  }
  for (uint32_t i = 0; i < sz; ++i)
  {
    avlNode *node = avlOffset(min, (int64_t)i);
    assert(container_of(node, Data, node)->val == i);

    for (uint32_t j = 0; j < sz; ++j)
    {
      int64_t offset = (int64_t)j - (int64_t)i;
      avlNode *n2 = avlOffset(node, offset);
      assert(container_of(n2, Data, node)->val == j);
    }
    assert(!avlOffset(node, -(int64_t)i - 1));
    assert(!avlOffset(node, sz - i));
  }

  dispose(c.root);
}

int main()
{
  for (uint32_t i = 1; i < 500; ++i)
  {
    test_case(i);
  }
  return 0;
}
