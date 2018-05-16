// 相互排他スピンロック。

#include "types.h"
#include "defs.h"
#include "param.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

// ロックを獲得する。
// ロックが獲得されるまでループ（スピン）する。
// ロックを長時間保持すると、他のCPUがロックを
// 獲得するためのスピンに時間を浪費させる可能性がある。
void
acquire(struct spinlock *lk)
{
  pushcli(); // ヘッドロックを避けるために割り込みを禁止する。
  if(holding(lk))
    panic("acquire");

  // xchgはアトミックである。
  while(xchg(&lk->locked, 1) != 0)
    ;

  // クリティカルセクションのメモリ参照がロックの獲得後に行われるように、
  // この点を超えるロード/ストアの移動をしないように
  // Cコンパイラとプロセッサに指示する。（メモリバリア）
  __sync_synchronize();

  // デバッグ用にロック獲得に関する情報を記録する。
  lk->cpu = mycpu();
  getcallerpcs(&lk, lk->pcs);
}

// ロックを解放する。
void
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  lk->pcs[0] = 0;
  lk->cpu = 0;

  // クリティカルセクションのすべてのストアがロックが解放される前に、
  // 他のコアから見られるようにするため（逆?）に、この点を超えるロード/ストアの移動を
  // しないように、Cコンパイラとプロセッサに指示する。
  // Cコンパイラとプロセッサは共にロードとストアを再配置する可能性がある。
  // __sync_synchronize() は両者にそれをしないように指示する。
  __sync_synchronize();

  // ロックの解放は、lk->locked = 0 に相当する。
  // このコードにCの代入は使えない。Cの代入はアトミックで
  // ないからである。実際のOSはCのアトミック関数をここに使うだろう。
  asm volatile("movl $0, %0" : "+m" (lk->locked) : );

  popcli();
}

// %ebpチェインをたどり、pcs[]に現在のコーススタックを記録する。
void
getcallerpcs(void *v, uint pcs[])
{
  uint *ebp;
  int i;

  ebp = (uint*)v - 2;
  for(i = 0; i < 10; i++){
    if(ebp == 0 || ebp < (uint*)KERNBASE || ebp == (uint*)0xffffffff)
      break;
    pcs[i] = ebp[1];     // 保存されていた%eip
    ebp = (uint*)ebp[0]; // 保存されていた%ebp
  }
  for(; i < 10; i++)
    pcs[i] = 0;
}

// このCPUがロックを保持しているかチェックする。
int
holding(struct spinlock *lock)
{
  return lock->locked && lock->cpu == mycpu();
}


// Pushcli/popcliは両者が対等であることを除いて、cli/stiと同じである:
// 2回のpushcliを取り消すには2回のpopcliが必要である。また、割り込みが
// オフであれば、pushcliとpopcliは割り込みをオフのままにする。

void
pushcli(void)
{
  int eflags;

  eflags = readeflags();
  cli();
  if(mycpu()->ncli == 0)
    mycpu()->intena = eflags & FL_IF;
  mycpu()->ncli += 1;
}

void
popcli(void)
{
  if(readeflags()&FL_IF)
    panic("popcli - interruptible");
  if(--mycpu()->ncli < 0)
    panic("popcli");
  if(mycpu()->ncli == 0 && mycpu()->intena)
    sti();
}
