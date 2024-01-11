/*
 * socket/tcp-write.c --
 *
 *	The tcp_write() function behaves like write(), but handles
 *	interrupted system calls and short writes. It also handles
 *	non-blocking file descriptors.
 */

#define _POSIX_C_SOURCE 201112L

#include <errno.h>
#include <unistd.h>

#include "tcp.h"

ssize_t tcp_write(int fd, const void *buf, size_t count) {
  size_t nwritten = 0;

  while (count > 0) {
    int r = write(fd, buf, count);
    if (r < 0 && errno == EINTR) {
      continue;
    }
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      return nwritten;
    }
    if (r < 0) {
      return r;
    }
    if (r == 0) {
      return nwritten;
    }
    buf = (unsigned char *)buf + r;
    count -= r;
    nwritten += r;
  }

  return nwritten;
}
