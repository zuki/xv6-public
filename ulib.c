#include "stat.h"
#include "user.h"

int
xv6_stat(char *path, struct stat *st)
{
  int fd;
  int r;

  fd = open(path, 0);
  if(fd < 0)
    return -1;
  r = fstat(fd, st);
  close(fd);
  return r;
}
