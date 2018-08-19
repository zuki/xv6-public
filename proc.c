#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
    struct spinlock lock;
    struct proc proc [NPROC];
} ptable;

static struct proc * initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1( void * chan );

void
pinit(void)
{
  initlock( &ptable.lock, "ptable" );
}

// 割り込みを禁止してから呼び出す必要がある
int
cpuid() {
  return mycpu()-cpus;
}

// lapicidの読み込みとループ実行の間に呼び出し側が再スケジュール
// されないように、割り込みを禁止してから呼び出す必要がある
struct cpu*
mycpu(void)
{
  int apicid, i;

  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDは連続であるとは限らない。リバースマップを持つか
  // &cpus[i]を格納するレジスタを用意するべきだろう。
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// cpu構造体からprocを読み込む間に再スケジュールされないように、
// 割り込みを禁止する
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// プロセステーブルから状態がUNUSEDのprocを探す。
// 見つかったら、状態をEMBRYOに変更し、カーネルで実行するために
// 必要な状態の初期化を行う。
// 見つからなかった場合は、0を返す。
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // カーネルスタックを割り当てる。
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // トラップフレーム用のスペースを確保する。
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // forkretから実行を開始するために新たなコンテキストを設定する。
  // forkretからはtrapretに復帰する。
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  // alarm関係の初期化
  p->cputicks = 0;
  p->alarmticks = 0;
  p->alarmhandler = (void (*)())0;

  return p;
}

//PAGEBREAK: 32
// 最初のユーザプロセスを設定する。
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // initcode.Sの先頭

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // このp->stateへの代入により、他のコアがこのプロセスを実行
  // できるようになる。acquireは上の書き込みを可視化する。
  // また、代入はアトミックではない可能性もあるので、
  // ロックが必要である。
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// カレントプロセスのメモリをnバイト増減する。
// 成功した場合は0、失敗した場合は-1を返す。
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// pを親としてコピーして新しいプロセスを作成する。
// システムコールから復帰したかのような復帰用のスタックを構成する。
// 呼び出し側は返されたprocのstateをRUNNABLEにセットしなければならない。（Obsolute: copyproc）
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // プロセスを割り当てる。
  if((np = allocproc()) == 0){
    return -1;
  }

  // curprocからプロセスの状態をコピーする。
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // 子プロセスではforkが0を返すように%eaxをクリアする。
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// カレントプロセスを終了する。復帰しない。
// 終了したプロセスは、親プロセスがwait()を呼び出してそれが終了したことを
// 知るまで、zombie状態で残る。
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // 開いていたファイルをすべて閉じる。
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // 親プロセスはwait()でスリープしている可能性がある。
  wakeup1(curproc->parent);

  // 子プロセスは見捨ててinitに渡す。
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // スケジューラにジャンプし、復帰しない。
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// 子プロセスが終了してpidを返すのを待つ。
// このプロセスが子プロセスを持たない場合は -1 を返す。
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // テーブルを走査して終了した子プロセスを探す。
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // 見つけた。
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // 子プロセスを持っていなければ待っても意味がない。
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // 子プロセスの終了を待つ。(proc_exitのwakeup1コールを参照)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// CPUごとのプロセススケジューラ。
// 各CPUは自身を設定した後 scheduler() を呼び出す。
// スケジューラは復帰しない。ループして以下を行う:
//  - 実行するプロセスを選択する
//  - swtchを呼び出してそのプロセスの実行を開始する
//  - 最終的にそのプロセスはスswtchを呼び出して
//      スケジューラに制御を戻す
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for(;;){
    // このプロセッサ上での割り込みを有効にする。
    sti();

    // プロセステーブルを走査して実行するプロセスを探す、
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // 選択したプロセスにスイッチする。ptable.lockを解放して、
      // スケジューラに戻る前に再度ロックするのは
      // プロセスの仕事である。
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // ここではプロセスは実行を終えている。
      // プロセスはここに戻る前に自分でp->stateを変更しているするはずだ。
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// スケジューラに入る。ptable.lockだけを保持し、
// proc->stateが変更されていなければならない。
// intenaは、このカーネルスレッドの属性であり
// このCPUの属性ではないので、intenaの保存と復元を行う。
// 本来ならproc->intenaとproc->ncliとするべきだが、それだと
// ロックは保持されているがプロセスがないような場所で
// まれに破綻する可能性がある。
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// 1回のスケジュール処理ごとにCPUを明け渡す。
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// scheduler()でスケジュールされる一番最初のフォークの子プロセスは
// ここにスイッチする。ユーザ空間に「復帰する」。
void
forkret(void)
{
  static int first = 1;
  // スケジューラからの ptable.lock をまだ保持しているので
  release(&ptable.lock);

  if (first) {
    // ある種の初期化関数は通常のプロセスのコンテキストで実行
    // されなければならない（たとえば、スリープのコールなど）。
    // そのため、main()からは実行することができない。
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // 「呼び出し元」に戻るが、実際はtrapretに戻る（allocprocを参照）。
}

// アトミックにロックを解放し、chanでスリープする。
// 起床した時にロックを再度獲得する。
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // p->stateを変更し、schedを呼び出すために
  // ptable.lockを獲得しなければならない。
  // ptable.lockを保持すれば, 起こし忘れが
  // ないことが保証される（wakeupはptable.lockが
  // ロックされた状態で実行する）ので,
  // lkを解放しても問題はない。
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // スリープに入る。
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // 後片付けをする。
  p->chan = 0;

  // 元々保持していたロックを再度獲得する。
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// chanでスリープしているすべてのプロセスを起床させる。
// ptable.lockを保持していなければならない。
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// chanでスリープしているすべてのプロセスを起床させる。
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// 指定されたpidを持つプロセスをkillする。
// プロセスはユーザ空間に復帰するまでは
// 終了しない（trap.cのtrapを参照）。
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // 必要であれば寝ているプロセスを起床させる。
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// プロセス一覧をコンソールに出力する。デバッグ用。
// ユーザがコンソールで^Pとタイプすると実行する。
// スタックしたマシンをさらに割り込ませないようにロックはしない。
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
