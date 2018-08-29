#include "types.h"
#include "fcntl.h"
#include "user.h"

#define BUFSIZE 412

char buf[BUFSIZE];

extern void exit(int);
int strlen(char *);

void
print(char *message, char *arg) {
  write(1, message, strlen(message));
  if (arg) write(1, arg, strlen(arg));
  write(1, "\n", 1);
}

void
cat(int fd)
{
  int n;

  while((n = read(fd, buf, BUFSIZE)) > 0) {
    if (write(1, buf, n) != n) {
      print("cat: write error", 0);
      exit(1);
    }
  }
  if(n < 0){
    print("cat: read error", 0);
    exit(1);
  }
}

void
main(int argc, char *argv[])
{
  int fd;
  int i;

  if(argc <= 1){
    cat(0);
    exit(0);
  }

  for(i = 1; i < argc; i++){
    if((fd = open(argv[i], O_RDONLY)) < 0){
      print("cat: cannot open ", argv[i]);
      exit(1);
    }
    cat(fd);
    close(fd);
  }
  exit(0);
}
