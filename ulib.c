#include "types.h"
#include "stat.h"
#include "user.h"
#include "x86.h"

char*
strcpy(char *s, const char *t)
{
  char *os;

  os = s;
  while((*s++ = *t++) != 0)
    ;
  return os;
}

int
strcmp(const char *p, const char *q)
{
  while(*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

size_t
strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}

void*
memset(void *dst, int c, size_t n)
{
  stosb(dst, c, n);
  return dst;
}

char*
strchr(const char *s, int c)
{
  for(; *s; s++)
    if(*s == c)
      return (char*)s;
  return 0;
}

char*
xv6_gets(char *buf, int max)
{
  int i, cc;
  char c;

  for(i=0; i+1 < max; ){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    buf[i++] = c;
    if(c == '\n' || c == '\r')
      break;
  }
  buf[i] = '\0';
  return buf;
}
int
atoi(const char *s)
{
  int n;

  n = 0;
  while('0' <= *s && *s <= '9')
    n = n*10 + *s++ - '0';
  return n;
}

void*
memmove(void *vdst, const void *vsrc, size_t n)
{
  char *dst;
  const char *src;

  dst = vdst;
  src = vsrc;
  while(n-- > 0)
    *dst++ = *src++;
  return vdst;
}

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

static void
xv6_putc(int fd, char c)
{
  write(fd, &c, 1);
}

static void
xv6_printint(int fd, int xx, int base, int sgn)
{
  static char digits[] = "0123456789ABCDEF";
  char buf[16];
  int i, neg;
  uint x;

  neg = 0;
  if(sgn && xx < 0){
    neg = 1;
    x = -xx;
  } else {
    x = xx;
  }

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);
  if(neg)
    buf[i++] = '-';

  while(--i >= 0)
    xv6_putc(fd, buf[i]);
}

// Print to the given fd. Only understands %d, %x, %p, %s.
void
xv6_printf(int fd, char *fmt, ...)
{
  char *s;
  int c, i, state;
  uint *ap;

  state = 0;
  ap = (uint*)(void*)&fmt + 1;
  for(i = 0; fmt[i]; i++){
    c = fmt[i] & 0xff;
    if(state == 0){
      if(c == '%'){
        state = '%';
      } else {
        xv6_putc(fd, c);
      }
    } else if(state == '%'){
      if(c == 'd'){
        xv6_printint(fd, *ap, 10, 1);
        ap++;
      } else if(c == 'x' || c == 'p'){
        xv6_printint(fd, *ap, 16, 0);
        ap++;
      } else if(c == 's'){
        s = (char*)*ap;
        ap++;
        if(s == 0)
          s = "(null)";
        while(*s != 0){
          xv6_putc(fd, *s);
          s++;
        }
      } else if(c == 'c'){
        xv6_putc(fd, *ap);
        ap++;
      } else if(c == '%'){
        xv6_putc(fd, c);
      } else {
        // Unknown % sequence.  Print it to draw attention.
        xv6_putc(fd, '%');
        xv6_putc(fd, c);
      }
      state = 0;
    }
  }
}
