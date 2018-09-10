#ifndef _XV6_STDIO_H
#define _XV6_STDIO_H

#include <stdarg.h>
#include <stddef.h>

#ifdef NULL
#undef NULL
#endif

#define NULL 0
#define EOF (-1)
#define BUFSIZ 1024
#define OPEN_MAX 10

#ifndef SEEK_SET
#define SEEK_SET        0       /* set file offset to offset */
#endif
#ifndef SEEK_CUR
#define SEEK_CUR        1       /* set file offset to current plus offset */
#endif
#ifndef SEEK_END
#define SEEK_END        2       /* set file offset to EOF plus offset */
#endif


typedef struct _iobuf {
  int  cnt;    /* characters left */
  char *ptr;   /* next character position */
  char *base;  /* location of the buffer */
  int  flag;   /* mode of the file access */
  int  fd;     /* file descriptor */
} FILE;

extern FILE _iob[OPEN_MAX];

#define stdin  (&_iob[0])
#define stdout (&_iob[1])
#define stderr (&_iob[2])

enum _flags {
  _READ  = 01,
  _WRITE = 02,
  _UNBUF = 04,
  _EOF   = 010,
  _ERR   = 020,
  _LNBUF = 040
};

int _fillbuf(FILE *);
int _flushbuf(int, FILE *);
int fflush(FILE *);

#define feof(p)     (((p)->flag & _EOF) != 0)
#define ferror(p)   (((p)->flag & _ERR) != 0)
#define fileno(p)   ((p)->fd)

#define getc(p)     fgetc(p)
#define putc(x, p)  fputc(x, p)
#define getchar()   getc(stdin)
#define putchar(x)  putc((x), stdout)


int fputc(int, FILE *);
int fgetc(FILE *);
int puts(char *);
int fputs(char *, FILE *);
char *fgets(char *, int, FILE *);
char *gets(char *);

FILE *fopen(const char *, const char *);
FILE *freopen(const char *, const char *, FILE *);
int fclose(FILE *);
int fseek(FILE *, long, int);

int printf(const char *, ...);
int fprintf(FILE *, const char *, ...);
int vprintf(const char *, va_list);
int vfprintf(FILE *, const char *, va_list);
int sprintf(char *, const char *, ...);

#endif  /* stdio.h */
