#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "user.h"

char buf[512];

void
wc(FILE *f, char *name)
{
  int i, n;
  int l, w, c, inword;

  l = w = c = 0;
  inword = 0;
  while((n = fread(buf, 1, sizeof(buf), f)) > 0){
    for(i=0; i<n; i++){
      c++;
      if(buf[i] == '\n')
        l++;
      if(strchr(" \r\t\n\v", buf[i]))
        inword = 0;
      else if(!inword){
        w++;
        inword = 1;
      }
    }
  }
  if(n < 0){
    printf("wc: read error\n");
    exit(1);
  }
  printf("%d %d %d %s\n", l, w, c, name);
}

int
main(int argc, char *argv[])
{
  FILE *f;
  int i;

  if(argc <= 1){
    wc(stdin, "");
    exit(0);
  }

  for(i = 1; i < argc; i++){
    if((f = fopen(argv[i], "r")) == NULL){
      printf("wc: cannot open %s\n", argv[i]);
      exit(1);
    }
    wc(f, argv[i]);
    fclose(f);
  }
  exit(0);
}
