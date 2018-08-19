#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// 割り込みディスクリプタテーブル（すべてのCPUで共有される）
struct gatedesc idt[256];
extern uint vectors[];  // vectors.Sで設定: 256エントリポインタの配列
struct spinlock tickslock;
uint ticks;
int mappages(pde_t *, void *, uint, uint, int);

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // BochsはスプリアスIDE1割り込みを生成する。
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // カーネルで発生。OSの問題に違いない。
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    if (tf->trapno == T_PGFLT) {
      uint a, oldsz, newsz;
      char *mem;

      oldsz = rcr2();
      newsz = myproc()->sz;
      if (newsz >= oldsz) {
        if(newsz >= KERNBASE)
          goto destroy;
        a = PGROUNDDOWN(oldsz);
        for(; a < newsz; a += PGSIZE){
          mem = kalloc();
          if(mem == 0){
            cprintf("allocuvm out of memory\n");
            deallocuvm(myproc()->pgdir, newsz, oldsz);
            goto destroy;
          }
          memset(mem, 0, PGSIZE);
          if (mappages(myproc()->pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
            cprintf("allocuvm out of memory (2)\n");
            deallocuvm(myproc()->pgdir, newsz, oldsz);
            kfree(mem);
            goto destroy;
          }
        }
      } else {
        if ((newsz = deallocuvm(myproc()->pgdir, newsz, oldsz)) == 0)
          goto destroy;
      }
      break;
    }
destroy:
    // ユーザ空間で発生。プロセスが不正を行ったようだ。
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // プロセスがkillされており、ユーザ空間のプロセスであれば、
  // プロセスを終了させる（カーネルで実行中の場合は、通常のシステム
  // コールの復帰を確認するまで実行を継続させる）。
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // クロック割り込みの場合はCPUを放棄させる。
  // (訳注: Obsolute)割り込みがロックの保持中にあったか否かをnlockでチェックする必要があるだろう。
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // yield中にプロセスがkillされたか否かをチェックする。
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
