#include <sys/socket.h>
#include <netinet/in.h>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <cassert>
#include <vector>
#include <string>

enum
{
  TAG_NIL = 0,
  TAG_ERROR = 1,
  TAG_STRING = 2,
  TAG_INTEGER = 3,
  TAG_DOUBLE = 4,
  TAG_ARRAY = 5
};

const size_t kMaxMsg = 4096;

static void die(const char *s)
{
  // int err = errno;
  perror(s);
  abort();
}

void msg(const char *s)
{
  perror(s);
  // exit(1);
}

static int32_t readAll(int fd, char *s, size_t len)
{
  while (len > 0)
  {
    ssize_t rv = read(fd, s, len);
    if (rv <= 0)
    {
      return -1;
    }
    assert((size_t)rv <= len);
    len -= (size_t)rv;
    s += rv;
  }
  return 0;
}

static int32_t writeAll(int fd, const char *buf, size_t len)
{
  while (len > 0)
  {
    ssize_t rv = write(fd, buf, len);
    if (rv <= 0)
    {
      return -1;
    }
    assert((size_t)rv <= len);
    len -= (size_t)rv;
    buf += rv;
  }
  return 0;
}

static void appendBuffer(std::vector<uint8_t> &buf, const uint8_t *data, size_t len)
{
  buf.insert(buf.end(), data, data + len);
}

static int32_t sendRequest(int fd, const std::vector<std::string> &cmd)
{
  uint32_t len = 4;
  for (const std::string &s : cmd)
  {
    len += 4 + s.size();
  }

  if (len > kMaxMsg)
  {
    return -1;
  }

  char wbuf[4 + kMaxMsg];
  memcpy(&wbuf[0], &len, 4);
  uint32_t n = (uint32_t)cmd.size();
  memcpy(&wbuf[4], &n, 4);
  size_t cur = 8;

  for (const std::string &s : cmd)
  {
    uint32_t p = (uint32_t)s.size();
    memcpy(&wbuf[cur], &p, 4);
    memcpy(&wbuf[cur + 4], s.data(), s.size());
    cur += 4 + s.size();
  }
  // memcpy(&wbuf[4], s, size_t(len));
  return writeAll(fd, wbuf, 4 + len);

  // if (err)
  // {
  //   return err;
  // }
  return 0;
}

static int32_t printResponse(const uint8_t *data, size_t size)
{
  if (size < 1)
  {
    msg("Bad Response");
    return -1;
  }
  uint32_t len = 0;
  int64_t val = 0;
  double dValue = 0;
  int32_t code = 0;
  size_t arrBytes = 1 + 4;

  printf("Data: %d\n", data[0]);
  switch (data[0])
  {
  case TAG_NIL:
    printf("(nil) \n");
    return 1;
  case TAG_ERROR:
    if (size < 9)
    {
      msg("Bad Response");
      return -1;
    }

    memcpy(&code, &data[1], 4);
    memcpy(&len, &data[1 + 4], 4);
    if (size < 9 + len)
    {
      msg("Bad Response");
      return -1;
    }
    printf("(err) %d %.*s\n", code, len, &data[1 + 8]);
    return 9 + len;
  case TAG_STRING:
    if (size < 5)
    {
      msg("Bad Response");
      return -1;
    }

    memcpy(&len, &data[1], 4);

    if (size < 5 + len)
    {
      msg("Bad Response");
      return -1;
    }
    printf("(str) %.*s\n", len, &data[5]);
    return 5 + len;
  case TAG_INTEGER:
    if (size < 9)
    {
      msg("Bad Response");
      return -1;
    }
    memcpy(&val, &data[1], 8);
    printf("(int) %ld\n", val);
    return 9;
  case TAG_DOUBLE:
    if (size < 9)
    {
      msg("Bad Response");
      return -1;
    }
    memcpy(&dValue, &data[1], 8);
    printf("(dbl) %g\n", dValue);
    return 9;
  case TAG_ARRAY:
    if (size < 5)
    {
      msg("Bad Response");
      return -1;
    }

    memcpy(&len, &data[1], 4);
    printf("(arr) len=%u\n", len);

    for (uint32_t i = 0; i < len; i++)
    {
      int32_t rv = printResponse(&data[arrBytes], size - arrBytes);
      if (rv < 0)
      {
        return rv;
      }
      arrBytes += (size_t)rv;
    }
    printf("(arr) end\n");
    return (int32_t)arrBytes;

  default:
    msg("Bad response");
    return -1;
  }
}

static int32_t readResponse(int fd)
{
  char rbuf[4 + kMaxMsg + 1];
  errno = 0;
  if (readAll(fd, rbuf, 4))
  {
    msg(errno == 0 ? "EOF" : "read() error");
    return -1;
  }

  uint32_t len = 0;
  memcpy(&len, rbuf, 4);
  if (len > kMaxMsg)
  {
    msg("msg too long");
    return -1;
  }

  int err = readAll(fd, &rbuf[4], len);
  if (err)
  {
    msg("read() error");
    return err;
  }

  int32_t rv = printResponse((uint8_t *)&rbuf[4], len);
  if (rv > 0 && (uint32_t)rv != len)
  {
    msg("Bad Response");
    rv = -1;
  }

  return rv;
}

int main(int argc, char **argv)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    die("socket() error");
  }

  struct sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = ntohs(1234);
  // INADDR_LOOPBACK = 127.0.0.1
  addr.sin_addr.s_addr = ntohl(INADDR_LOOPBACK);
  socklen_t addr_len = sizeof(addr);
  int rv = connect(fd, (const sockaddr *)&addr, addr_len);
  if (rv < 0)
  {
    die("connect()");
  }

  // std::vector<std::string> queryList = {
  //     "get",
  //     "set",
  //     "",
  //     std::string(kMaxMsg, 'z'),
  //     "hello5",
  // };
  std::vector<std::string> cmd;
  for (int i = 1; i < argc; i++)
  {
    printf("Command is: %s\n", argv[i]);
    cmd.push_back(argv[i]);
  }

  int32_t err = sendRequest(fd, cmd);
  if (err)
  {
    close(fd);
    return 0;
  }

  // for (size_t i = 0; i < queryList.size(); i++)
  // {
  err = readResponse(fd);
  if (err)
  {
    close(fd);
    return 0;
  }
  // }

  close(fd);
  return 0;
}