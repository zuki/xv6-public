// 物理メモリアロケータ。ユーザプロセス、カーネルスタック、
// ページテーブルページ、パイプバッファのためのメモリの割り当てを行う。
// 4096バイトのページを割り当てる

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

void freerange(void *vstart, void *vend);
extern char end[]; // ELFファイルからロードされたカーネルに続く最初のアドレス。
                   // kernel.ldにあるカーネルリンカスクリプトで定義されている

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
} kmem;

// 初期化は２段階で行われる。
// 1. main()はkinit1を呼び出す。この時にはまだentrypgdirが使用されており、
// entrypgdirでマッピングされたページを空きリストに設定する。
// 2. main()はkinit2()を呼び出す。すべてのコアで物理ページをマッピングする
// 完全なページテーブルをインストールした後に、残りの物理ページを初期化する。
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree(p);
}
//PAGEBREAK: 21
// vで指し示される物理メモリのページを解放する。
// vは通常、kalloc()の呼び出しで返されたポインタである。
// （例外は、アロケータを初期化する場合である。
// 上のkinitを参照）
void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree");

  // ダングリング参照をキャッチするためにジャンクで満たす
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = (struct run*)v;
  r->next = kmem.freelist;
  kmem.freelist = r;
  if(kmem.use_lock)
    release(&kmem.lock);
}

// 4096バイト単位の物理メモリページを1ページ割り当てる。
// カーネルが利用可能なポインタを返す。
// メモリを割り当てられなかった場合は、0を返す。
char*
kalloc(void)
{
  struct run *r;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  if(kmem.use_lock)
    release(&kmem.lock);
  return (char*)r;
}
