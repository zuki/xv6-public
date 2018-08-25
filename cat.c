#include <stdio.h>
#include <stdlib.h>
#include "types.h"
#include "stat.h"
#include "user.h"

char buf[512];

void
cat(FILE *f)
{
  int n;

  while((n = fread(buf, sizeof(char), sizeof(buf), f)) > 0) {
    if (fwrite(buf, sizeof(char), n, stdout) != n) {
      printf("cat: write error\n");
      exit(1);
    }
  }
  if(n < 0){
    printf("cat: read error\n");
    exit(1);
  }
}

int
main(int argc, char *argv[])
{
  FILE *f;
  int i;

  if(argc <= 1){
    cat(stdin);
    exit(0);
  }

  for(i = 1; i < argc; i++){
    if((f = fopen(argv[i], "r")) == NULL){
      printf("cat: cannot open %s\n", argv[i]);
      exit(1);
    }
    cat(f);
    fclose(f);
  }
  exit(0);
}
