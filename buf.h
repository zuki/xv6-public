#ifndef _XV6_BUF_H
#define _XV6_BUF_H

#include "sleeplock.h"

struct buf {
  int flags;
  uint dev;
  uint blockno;
  struct sleeplock lock;
  uint refcnt;
  struct buf *prev; // LRUキャッシュリスト
  struct buf *next;
  struct buf *qnext; // ディスクキュー
  uchar data[BSIZE];
};
#define B_VALID 0x2  // バッファはディスクから読み込まれている
#define B_DIRTY 0x4  // バッファをディスクに書き込む必要がある

#endif