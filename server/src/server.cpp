
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <cassert>
#include <cerrno>
#include <time.h>
#include <math.h>
#include <cstring>
#include <fcntl.h>
#include <poll.h>

// #include <map>
#include <string>
#include <vector>

#include <common.h>
#include <hashtable.h>
#include <zset.h>
#include <heap.h>
#include <doublelinklist.h>
#include <threadpool.h>

typedef std::vector<uint8_t> Buffer;

struct Conn
{
  int fd = -1;

  bool want_write = false;
  bool want_read = false;
  bool want_close = false;

  Buffer incoming;
  Buffer outgoing;

  uint64_t lastActiveMS = 0;
  DList idleNode;
};

static struct
{
  HMap database;
  std::vector<Conn *> fd2conn;
  DList idleList;
  std::vector<HeapItem> heap;
  ThreadPool threadPool;
} gData;

const size_t kMaxMsg = (32 << 20);
const size_t kMaxArgs = (200 * 1000);
const uint64_t kIdleTimeoutMS = 5 * 1000;

enum
{
  RES_OK = 0,
  RES_ERROR,
  RES_NX
};

enum
{
  ERROR_UNKNOWN = 1,
  ERROR_TOO_BIG,
  ERROR_BAD_TYPE,
  ERROR_BAD_ARGUMENT
};

enum
{
  TAG_NIL = 0,
  TAG_ERROR,
  TAG_STRING,
  TAG_INTEGER,
  TAG_DOUBLE,
  TAG_ARRAY,
};

enum
{
  T_INIT = 0,
  T_STRING,
  T_ZSET
};

struct LookupKey
{
  struct HNode node;
  std::string key;
};

struct Entry
{
  struct HNode node;
  std::string key;

  size_t heapIndex = -1;
  uint32_t type = 0;

  std::string string;

  ZSet zset;
};

static const ZSet kEmptyZSet;

static void die(const char *s)
{
  perror(s);
  exit(1);
}

static void msg(const char *s)
{
  perror(s);
}

static uint64_t GetMonotonicMSec()
{
  struct timespec tv = {0, 0};
  clock_gettime(CLOCK_MONOTONIC, &tv);
  return uint64_t(tv.tv_sec) * 1000 + tv.tv_nsec / 1000 / 1000;
}

static void fdSetNonBlock(int fd)
{
  errno = 0;
  int flags = fcntl(fd, F_GETFL, 0);
  if (errno)
  {
    die("fcntl get error");
    return;
  }
  flags |= O_NONBLOCK;

  errno = 0;
  fcntl(fd, F_SETFL, flags);
  if (errno)
  {
    die("fcntl set error");
  }
}

static void connDestroy(Conn *conn)
{
  (void)close(conn->fd);
  gData.fd2conn[conn->fd] = NULL;
  DListDetach(&conn->idleNode);
  delete conn;
}

static bool stringToDouble(const std::string &s, double &out)
{
  char *endPoint = NULL;
  out = strtod(s.c_str(), &endPoint);
  return endPoint == s.c_str() + s.size() && !isnan(out);
}

static bool stringToInterger(const std::string &s, int64_t &out)
{
  char *endPoint = NULL;
  out = strtoll(s.c_str(), &endPoint, 10);
  return endPoint == s.c_str() + s.size();
}

static void appendBuffer(Buffer &buf, const uint8_t *data, size_t len)
{
  buf.insert(buf.end(), data, data + len);
}

static void appendBufferu8(Buffer &buf, const uint8_t data)
{
  buf.push_back(data);
}

static void appendBufferu32(Buffer &buf, const uint32_t data)
{
  appendBuffer(buf, (const uint8_t *)&data, 4);
}

static void appendBufferDouble(Buffer &buf, const double data)
{
  appendBuffer(buf, (const uint8_t *)&data, 8);
}

static void appendBufferi64(Buffer &buf, const int64_t data)
{
  appendBuffer(buf, (const uint8_t *)&data, 8);
}

static void consumeBuffer(std::vector<uint8_t> &buf, size_t len)
{
  buf.erase(buf.begin(), buf.begin() + len);
}

static bool readu32(const uint8_t *&data, const uint8_t *end, uint32_t &out)
{
  if (data + 4 > end)
  {
    return -1;
  }
  memcpy(&out, data, 4);
  data += 4;
  return true;
}

static bool readString(const uint8_t *&data, const uint8_t *end, size_t size, std::string &out)
{
  if (data + size > end)
  {
    return -1;
  }
  out.assign(data, data + size);
  data += size;
  return true;
}

static void HeapDelete(std::vector<HeapItem> &heap, size_t pos)
{
  heap[pos] = heap.back();
  heap.pop_back();

  if (pos < heap.size())
  {
    HeapUpdate(heap.data(), pos, heap.size());
  }
}

static void HeapUpsert(std::vector<HeapItem> &heap, size_t pos, HeapItem item)
{
  if (pos < heap.size())
  {
    heap[pos] = item;
  }
  else
  {
    pos = heap.size();
    heap.push_back(item);
  }
  HeapUpdate(heap.data(), pos, heap.size());
}

static void entrySetTTL(Entry *entry, int64_t ttl_ms)
{
  if (ttl_ms < 0 && entry->heapIndex != (size_t)-1)
  {
    HeapDelete(gData.heap, entry->heapIndex);
    entry->heapIndex = -1;
  }
  else
  {
    uint64_t expireAt = GetMonotonicMSec() + (uint64_t)ttl_ms;
    HeapItem item = {expireAt, &entry->heapIndex};
    HeapUpsert(gData.heap, entry->heapIndex, item);
  }
}

static Entry *entryNew(uint32_t type)
{
  Entry *entry = new Entry();
  entry->type = type;
  return entry;
}

static Entry *entryDeleteSync(Entry *entry)
{
  if (entry->type == T_ZSET)
  {
    ZSetClear(&entry->zset);
  }
  return entry;
}

static void entryDeleteFunc(void *arg)
{
  entryDeleteSync((Entry *)arg);
}

static void entryDelete(Entry *entry)
{
  entrySetTTL(entry, -1);

  size_t setSize = (entry->type == T_ZSET) ? HashMapSize(&entry->zset.hmap) : 0;
  const size_t kLargeContainerSize = 1000;
  if (setSize > kLargeContainerSize)
  {
    ThreadPoolQueue(&gData.threadPool, &entryDeleteFunc, entry);
  }
  else
  {
    entryDeleteSync(entry);
  }
}

static bool entryEqual(HNode *node, HNode *key)
{
  struct Entry *entry = containerOf(node, struct Entry, node);
  struct LookupKey *keyData = containerOf(key, struct LookupKey, node);
  return entry->key == keyData->key;
}

static void outputNil(Buffer &buf)
{
  appendBufferu8(buf, TAG_NIL);
}

static void outputString(Buffer &buf, const char *s, size_t size)
{
  appendBufferu8(buf, TAG_STRING);
  appendBufferu32(buf, (uint32_t)size);
  appendBuffer(buf, (const uint8_t *)s, size);
}

static void outputInteger(Buffer &buf, const int64_t val)
{
  appendBufferu8(buf, TAG_INTEGER);
  appendBufferi64(buf, val);
}

static void outputDouble(Buffer &buf, const double val)
{
  appendBufferu8(buf, TAG_DOUBLE);
  appendBufferDouble(buf, val);
}

static void outputError(Buffer &buf, uint32_t code, const std::string &msg)
{
  appendBufferu8(buf, TAG_ERROR);
  appendBufferu32(buf, code);
  appendBufferu32(buf, msg.size());
  appendBuffer(buf, (const uint8_t *)msg.data(), msg.size());
}

static void outputArray(Buffer &buf, const uint32_t n)
{
  appendBufferu8(buf, TAG_ARRAY);
  appendBufferu32(buf, n);
}

static size_t outputBeginArray(Buffer &buf)
{
  buf.push_back(TAG_ARRAY);
  appendBufferu32(buf, 0);
  return buf.size() - 4;
}

static void outputEndArray(Buffer &buf, size_t ctx, uint32_t n)
{
  assert(buf[ctx - 1] == TAG_ARRAY);
  memcpy(&buf[ctx], &n, 4);
}

static void doGet(std::vector<std::string> &cmd, Buffer &buf)
{
  LookupKey key;
  key.key.swap(cmd[1]);
  key.node.hcode = stringHash((const uint8_t *)key.key.data(), key.key.size());

  HNode *node = HashMapLookup(&gData.database, &key.node, &entryEqual);

  if (!node)
  {
    return outputNil(buf);
  }

  Entry *entry = containerOf(node, Entry, node);
  if (entry->type != T_STRING)
  {
    return outputError(buf, ERROR_BAD_TYPE, "Not a string value");
  }
  return outputString(buf, entry->string.data(), entry->string.size());
}

static void doSet(std::vector<std::string> &cmd, Buffer &buf)
{
  LookupKey key;
  key.key.swap(cmd[1]);
  key.node.hcode = stringHash((const uint8_t *)key.key.data(), key.key.size());

  HNode *node = HashMapLookup(&gData.database, &key.node, &entryEqual);
  if (node)
  {
    Entry *ent = containerOf(node, Entry, node);
    if (ent->type != T_STRING)
    {

      return outputError(buf, ERROR_BAD_TYPE, "a non-string value exist");
    }

    ent->string.swap(cmd[2]);
  }
  else
  {
    Entry *ent = entryNew(T_STRING);
    ent->key.swap(key.key);
    ent->node.hcode = key.node.hcode;
    ent->string.swap(cmd[2]);
    HashMapInsert(&gData.database, &ent->node);
  }

  return outputNil(buf);
}

static void doDel(std::vector<std::string> &cmd, Buffer &buf)
{
  LookupKey key;
  key.key.swap(cmd[1]);
  key.node.hcode = stringHash((const uint8_t *)key.key.data(), key.key.size());

  HNode *node = HashMapDelete(&gData.database, &key.node, &entryEqual);
  if (node)
  {
    entryDelete(containerOf(node, Entry, node));
  }

  return outputInteger(buf, node ? 1 : 0);
}

static bool cbKey(HNode *node, void *arg)
{
  Buffer &out = *(Buffer *)arg;
  const std::string &key = containerOf(node, Entry, node)->key;
  outputString(out, key.data(), key.size());
  return true;
}

static void doKey(std::vector<std::string> &, Buffer &buf)
{
  outputArray(buf, (uint32_t)HashMapSize(&gData.database));
  HashMapForEach(&gData.database, &cbKey, (void *)&buf);
}

static void doExpire(std::vector<std::string> &cmd, Buffer &buf)
{
  int64_t ttl_ms = 0;
  if (!stringToInterger(cmd[2], ttl_ms))
  {
    return outputError(buf, ERROR_BAD_ARGUMENT, "expect integer");
  }

  LookupKey key;
  key.key.swap(cmd[1]);
  key.node.hcode = stringHash((uint8_t *)key.key.data(), key.key.size());
  HNode *node = HashMapLookup(&gData.database, &key.node, &entryEqual);
  if (node)
  {
    Entry *entry = containerOf(node, Entry, node);
    entrySetTTL(entry, ttl_ms);
  }
  return outputInteger(buf, node ? 1 : 0);
}

static void doTTL(std::vector<std::string> &cmd, Buffer &buf)
{
  LookupKey key;
  key.key.swap(cmd[1]);
  key.node.hcode = stringHash((uint8_t *)key.key.data(), key.key.size());

  HNode *node = HashMapLookup(&gData.database, &key.node, &entryEqual);
  if (!node)
  {
    return outputInteger(buf, -2);
  }

  Entry *entry = containerOf(node, Entry, node);
  if (entry->heapIndex == (size_t)-1)
  {
    return outputInteger(buf, -1);
  }
  uint64_t expireAt = gData.heap[entry->heapIndex].val;
  uint64_t nowMS = GetMonotonicMSec();
  return outputInteger(buf, expireAt > nowMS ? (expireAt - nowMS) : 0);
}

static ZSet *ExpectZSet(std::string &s)
{
  LookupKey key;
  key.key.swap(s);
  key.node.hcode = stringHash((uint8_t *)key.key.data(), key.key.size());
  HNode *node = HashMapLookup(&gData.database, &key.node, &entryEqual);
  if (!node)
  {
    return (ZSet *)&kEmptyZSet;
  }
  Entry *entry = containerOf(node, Entry, node);
  return entry->type == T_ZSET ? &entry->zset : NULL;
}

static void doZAdd(std::vector<std::string> &cmd, Buffer &buf)
{
  double score = 0;
  if (!stringToDouble(cmd[2], score))
  {
    return outputError(buf, ERROR_BAD_ARGUMENT, "expect float");
  }

  LookupKey key;
  key.key.swap(cmd[1]);
  key.node.hcode = stringHash((uint8_t *)key.key.data(), key.key.size());
  HNode *node = HashMapLookup(&gData.database, &key.node, &entryEqual);

  Entry *entry = NULL;
  if (!node)
  {
    entry = entryNew(T_ZSET);
    entry->key.swap(key.key);
    entry->node.hcode = key.node.hcode;
    HashMapInsert(&gData.database, &entry->node);
  }
  else
  {
    entry = containerOf(node, Entry, node);
  }

  const std::string &name = cmd[3];
  bool added = ZSetInsert(&entry->zset, name.data(), name.size(), score);
  return outputInteger(buf, (int64_t)added);
}

static void doZRemove(std::vector<std::string> &cmd, Buffer &buf)
{
  ZSet *zset = ExpectZSet(cmd[1]);
  if (!zset)
  {
    return outputError(buf, ERROR_BAD_TYPE, "expecting zset");
  }

  const std::string &name = cmd[2];
  ZNode *znode = ZSetLookup(zset, name.data(), name.size());
  if (znode)
  {
    ZSetDelete(zset, znode);
  }
  return outputInteger(buf, znode ? 1 : 0);
}

static void doZScore(std::vector<std::string> &cmd, Buffer &buf)
{
  ZSet *zset = ExpectZSet(cmd[1]);
  if (!zset)
  {
    return outputError(buf, ERROR_BAD_TYPE, "expecting zset");
  }

  const std::string &name = cmd[2];
  ZNode *znode = ZSetLookup(zset, name.data(), name.size());
  return znode ? outputDouble(buf, znode->score) : outputNil(buf);
}

static void doZQuery(std::vector<std::string> &cmd, Buffer &buf)
{
  printf("ZQuery");
  double score = 0;
  if (stringToDouble(cmd[2], score))
  {
    return outputError(buf, ERROR_BAD_ARGUMENT, "expect floating point number");
  }

  const std::string &name = cmd[3];
  int64_t offset = 0;
  int64_t limit = 0;
  printf("Command: %s %s", cmd[4].data(), cmd[5].data());
  if (stringToInterger(cmd[4], offset) || stringToInterger(cmd[5], limit))
  {
    return outputError(buf, ERROR_BAD_ARGUMENT, "expect integer number");
  }

  ZSet *zset = ExpectZSet(cmd[1]);
  if (!zset)
  {
    return outputError(buf, ERROR_BAD_TYPE, "expext zset");
  }

  if (limit <= 0)
  {
    return outputArray(buf, 0);
  }

  ZNode *znode = ZSetSeekge(zset, score, name.data(), name.size());
  znode = ZNodeOffset(znode, offset);

  size_t ctx = outputBeginArray(buf);
  int64_t n = 0;

  while (znode && n < limit)
  {
    outputString(buf, znode->name, znode->len);
    outputDouble(buf, znode->score);
    znode = ZNodeOffset(znode, +1);
    n += 2;
  }
  outputEndArray(buf, ctx, (uint32_t)n);
}

static void doRequest(std::vector<std::string> &cmd, Buffer &buf)
{
  if (cmd.size() == 2 && cmd[0] == "get")
  {
    return doGet(cmd, buf);
  }
  else if (cmd.size() == 3 && cmd[0] == "set")
  {
    return doSet(cmd, buf);
  }
  else if (cmd.size() == 2 && cmd[0] == "del")
  {
    return doDel(cmd, buf);
  }
  else if (cmd.size() == 1 && cmd[0] == "keys")
  {
    return doKey(cmd, buf);
  }
  else if (cmd.size() == 3 && cmd[0] == "pexpire")
  {
    return doExpire(cmd, buf);
  }
  else if (cmd.size() == 2 && cmd[0] == "pttl")
  {
    return doTTL(cmd, buf);
  }
  if (cmd.size() == 4 && cmd[0] == "zadd")
  {
    return doZAdd(cmd, buf);
  }
  else if (cmd.size() == 3 && cmd[0] == "zrem")
  {
    return doZRemove(cmd, buf);
  }
  else if (cmd.size() == 3 && cmd[0] == "zscore")
  {
    return doZScore(cmd, buf);
  }
  else if (cmd.size() == 6 && cmd[0] == "zquery")
  {
    return doZQuery(cmd, buf);
  }
  else
  {
    outputError(buf, ERROR_UNKNOWN, "Unknown Command");
  }
}

static void responseBegin(Buffer &buf, size_t *header)
{
  *header = buf.size();
  appendBufferu32(buf, 0);
}

static size_t responseSize(Buffer &buf, size_t header)
{
  return buf.size() - header - 4;
}

static void responseEnd(Buffer &buf, size_t header)
{
  size_t msgSize = responseSize(buf, header);
  if (msgSize > kMaxMsg)
  {
    buf.resize(header + 4);
    outputError(buf, ERROR_TOO_BIG, "Message too big");
    msgSize = responseSize(buf, header);
  }

  uint32_t len = (uint32_t)msgSize;
  memcpy(&buf[header], &len, 4);
}

// static void makeResponse(const Buffer &resp, std::vector<uint8_t> &ongoing)
// {
//   uint32_t resp_len = 4 + (uint32_t)resp.data.size();
//   appendBuffer(ongoing, (const uint8_t *)&resp_len, 4);
//   appendBuffer(ongoing, (const uint8_t *)&resp.status, 4);
//   appendBuffer(ongoing, resp.data.data(), resp.data.size());
// }

static int32_t parseReq(const uint8_t *req, size_t size, std::vector<std::string> &out)
{
  const uint8_t *end = req + size;
  uint32_t nstr = 0;
  if (!readu32(req, end, nstr) < 0 || nstr > kMaxArgs)
  {
    return -1;
  }

  while (out.size() < nstr)
  {
    uint32_t len = 0;
    if (!readu32(req, end, len) < 0)
    {
      return -1;
    }
    out.push_back(std::string());
    if (!readString(req, end, len, out.back()))
    {
      return -1;
    }
  }

  if (req != end)
  {
    return -1;
  }
  return 0;
}

static bool try_one_request(Conn *conn)
{
  if (conn->incoming.size() < 4)
  {
    return false;
  }

  uint32_t len = 0;
  memcpy(&len, conn->incoming.data(), 4);
  if (len > kMaxMsg)
  {
    msg("msg too long");
    conn->want_close = true;
    return false;
  }

  if (4 + len > conn->incoming.size())
  {
    return false;
  }

  const uint8_t *request = &conn->incoming[4];

  std::vector<std::string> cmd;
  if (parseReq(request, len, cmd) < 0)
  {
    msg("Bad Request");
    conn->want_close = true;
    return false;
  }

  size_t header_pos = 0;
  responseBegin(conn->outgoing, &header_pos);
  doRequest(cmd, conn->outgoing);
  responseEnd(conn->outgoing, header_pos);

  consumeBuffer(conn->incoming, 4 + len);
  return true;
}

static int32_t handleAccept(int fd)
{
  struct sockaddr_in client_addr = {};
  socklen_t client_len = sizeof(client_addr);
  int conn_fd = accept(fd, (sockaddr *)&client_addr, &client_len);
  if (conn_fd < 0)
  {
    msg("accept() error");
    return -1;
  }

  uint32_t ip = client_addr.sin_addr.s_addr;
  fprintf(stderr, "New connection from: %u.%u.%u.%u:%u\n",
          ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, (ip >> 24) & 255,
          ntohs(client_addr.sin_port));

  fdSetNonBlock(conn_fd);

  Conn *conn = new Conn();
  conn->fd = conn_fd;
  conn->want_read = true;
  conn->lastActiveMS = GetMonotonicMSec();
  DListInsertBefore(&gData.idleList, &conn->idleNode);

  if (gData.fd2conn.size() <= (size_t)conn->fd)
  {
    gData.fd2conn.resize(conn->fd + 1);
  }

  assert(!gData.fd2conn[conn->fd]);
  gData.fd2conn[conn->fd] = conn;
  return 0;
}

static void handleWrite(Conn *conn)
{
  assert(conn->outgoing.size() > 0);
  ssize_t rv = write(conn->fd, &conn->outgoing[0], conn->outgoing.size());
  if (rv < 0)
  {
    if (errno == EAGAIN)
      return;
    msg("write() error");
    conn->want_close = true;
    return;
  }

  consumeBuffer(conn->outgoing, (size_t)rv);

  if (conn->outgoing.size() == 0)
  {
    conn->want_write = false;
    conn->want_read = true;
  }
}

static void handleRead(Conn *conn)
{
  uint8_t buf[64 * 1024];
  ssize_t rv = read(conn->fd, buf, sizeof(buf));
  if (rv < 0)
  {
    if (errno == EAGAIN)
      return;
    msg("read() error");
    conn->want_close = true;
    return;
  }

  if (rv == 0)
  {
    if (conn->incoming.size() == 0)
    {
      msg("Client is closed");
    }
    else
    {
      msg("unexpected EOF");
    }

    conn->want_close = true;
    return;
  }

  appendBuffer(conn->incoming, buf, (size_t)rv);

  while (try_one_request(conn))
  {
  }

  if (conn->outgoing.size() > 0)
  {
    conn->want_read = 0;
    conn->want_write = 1;
    return handleWrite(conn);
  }
}

static int32_t nextTimerMS()
{
  uint64_t nextMS = (size_t)-1;
  if (!DListEmpty(&gData.idleList))
  {
    return -1;
    Conn *conn = containerOf(gData.idleList.next, Conn, idleNode);
    nextMS = conn->lastActiveMS + kIdleTimeoutMS;
  }

  uint64_t nowMS = GetMonotonicMSec();

  if (!gData.heap.empty() && gData.heap[0].val < nextMS)
  {
    nextMS = gData.heap[0].val;
  }

  if (nextMS == (size_t)-1)
  {
    return -1;
  }
  if (nextMS <= nowMS)
  {
    return 0;
  }

  return (int32_t)(nextMS - nowMS);
}

static void processTimers()
{
  uint64_t nowMS = GetMonotonicMSec();
  while (!DListEmpty(&gData.idleList))
  {
    Conn *conn = containerOf(gData.idleList.next, Conn, idleNode);
    uint64_t nextMS = conn->lastActiveMS + kIdleTimeoutMS;
    if (nextMS >= nowMS)
    {
      break;
    }
    fprintf(stderr, "removing idle connection: %d\n", conn->fd);
    connDestroy(conn);
  }

  const size_t kMaxWork = 2000;
  size_t nworks = 0;
  const std::vector<HeapItem> &heap = gData.heap;
  while (!heap.empty() && heap[0].val < nowMS)
  {
    Entry *entry = containerOf(heap[0].ref, Entry, heapIndex);
    HNode *node = HashMapDelete(&gData.database, &entry->node, [](HNode *node, HNode *key)
                                { return node == key; });
    assert(node == &entry->node);
    entryDelete(entry);
    if (nworks++ >= kMaxWork)
    {
      break;
    }
  }
}

int main()
{
  DListInit(&gData.idleList);
  ThreadPoolInit(&gData.threadPool, 4);
  // AF_INET = Ip4
  // AF_INET6 = Ip6
  // SOCK_STREAM = TCP
  // SOCK_DGRAM = UDP
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    die("socket() error");
  }

  int val = 1;
  // SO_REUSEADDR to be able to reconnect to the same port
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(1234);
  addr.sin_addr.s_addr = htonl(0);
  int rv = bind(fd, (const struct sockaddr *)&addr, sizeof(addr));
  if (rv < 0)
  {
    die("bind()");
  }

  fdSetNonBlock(fd);

  // SOMAXCONN = Socket Max Connection, size of queue, which is 4096.
  rv = listen(fd, SOMAXCONN);
  if (rv < 0)
  {
    die("Listen()");
  }

  std::vector<struct pollfd> poll_args;

  while (true)
  {
    poll_args.clear();

    struct pollfd pfd = {fd, POLLIN, 0};

    poll_args.push_back(pfd);

    for (Conn *conn : gData.fd2conn)
    {
      if (!conn)
        continue;
      struct pollfd pfd = {conn->fd, POLLERR, 0};

      if (conn->want_read)
        pfd.events |= POLLIN;
      if (conn->want_write)
        pfd.events |= POLLOUT;

      poll_args.push_back(pfd);
    }

    int32_t timeoutMS = nextTimerMS();
    // Wait for a connections
    int rv = poll(poll_args.data(), (nfds_t)poll_args.size(), timeoutMS);
    if (rv < 0)
    {
      if (errno == EINTR)
        continue;
      die("poll() error");
    }

    // If there's a new connections, create a Conn object
    if (poll_args[0].revents)
    {
      handleAccept(fd);
    }

    // Handle the connection messages.
    for (size_t i = 1; i < poll_args.size(); i++)
    {
      uint32_t ready = poll_args[i].revents;
      if (!ready)
        continue;

      Conn *conn = gData.fd2conn[poll_args[i].fd];

      conn->lastActiveMS = GetMonotonicMSec();
      DListDetach(&conn->idleNode);
      DListInsertBefore(&gData.idleList, &conn->idleNode);

      if (ready & POLLIN)
      {
        assert(conn->want_read);
        handleRead(conn);
      }
      if (ready & POLLOUT)
      {
        assert(conn->want_write);
        handleWrite(conn);
      }

      if ((ready & POLLERR) || conn->want_close)
      {
        connDestroy(conn);
      }
    }
    processTimers();
  }
  return 0;
}