#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void startothers(void);
static void mpmain(void)  __attribute__((noreturn));
extern pde_t *kpgdir;
extern char end[]; // カーネルをELFファイルからロードした後の最初のアドレス

// ブートストラップププロセッサはここからCコードの実行を開始する。
// 実際のスタックを割り当て、それに切り替える。まず、メモリアロケータの
// 動作に必要な各種設定を行う
int
main(void)
{
  kinit1(end, P2V(4*1024*1024)); // 物理ページアロケータ
  kvmalloc();      // カーネルページテーブル
  mpinit();        // 他のプロセッサの検出
  lapicinit();     // 割り込みコントローラ
  seginit();       // セグメントデスクリプタ
  picinit();       // picの無効化
  ioapicinit();    // もう1つの割り込みコントローラ
  consoleinit();   // コンソールハードウェア
  uartinit();      // シリアルポート
  pinit();         // プロセステーブル
  tvinit();        // トラップベクタ
  binit();         // バッファキャッシュ
  fileinit();      // ファイルテーブル
  ideinit();       // ディスク
  startothers();   // 他のプロセッサの開始
  kinit2(P2V(4*1024*1024), P2V(PHYSTOP)); // startothers()の後でなければならない
  userinit();      // 最初のユーザプロセス
  mpmain();        // このプロセッサの設定を終了
}

// 他のCPUはentryother.Sからここにジャンプする
static void
mpenter(void)
{
  switchkvm();
  seginit();
  lapicinit();
  mpmain();
}

// 共通のCPU設定コード
static void
mpmain(void)
{
  cprintf("cpu%d: starting %d\n", cpuid(), cpuid());
  idtinit();       // idtレジスタのロード
  xchg(&(mycpu()->started), 1); // startothers()にこのCPUの起動を伝える
  scheduler();     // プロセスの実行を開始する
}

pde_t entrypgdir[];  // entry.S用に

// 非ブート(AP)プロセッサを開始する
static void
startothers(void)
{
  extern uchar _binary_entryother_start[], _binary_entryother_size[];
  uchar *code;
  struct cpu *c;
  char *stack;

  // エントリコードを0x7000の未使用メモリに書き込む
  // リンカはentryother.Sのイメージを_binary_entryother_startに
  // 置いている。
  code = P2V(0x7000);
  memmove(code, _binary_entryother_start, (uint)_binary_entryother_size);

  for(c = cpus; c < cpus+ncpu; c++){
    if(c == mycpu())  // ブートCPUは開始済み
      continue;

    // どのスタックを使うのか、どこにenterするのか、どのpgdirを使うのかを
    // entryother.Sに伝える。APプロセッサは低位アドレスで実行しているので
    // kpgdirはまだ使えない。そのため、APにもentrypgdirを使用する。
    stack = kalloc();
    *(void**)(code-4) = stack + KSTACKSIZE;
    *(void**)(code-8) = mpenter;
    *(int**)(code-12) = (void *) V2P(entrypgdir);

    lapicstartap(c->apicid, V2P(code));

    // cpuがmpmain()を終了するのを待機する
    while(c->started == 0)
      ;
  }
}

// entry.S と entryother.Sで使用するブートページテーブル.
// ページディレクトリ（とページエントリ）はページ境界から開始しなければ
// ならない。そのため __aligned__ 属性を指定する。
// ページディレクトリエントリの PTE_PS はページサイズを4Mバイトにする。

__attribute__((__aligned__(PGSIZE)))
pde_t entrypgdir[NPDENTRIES] = {
  // 仮想アドレス [0, 4MB) を物理アドレス [0, 4MB) にマッピング
  [0] = (0) | PTE_P | PTE_W | PTE_PS,
  // 仮想アドレス [KERNBASE, KERNBASE+4MB) を物理アドレス [0, 4MB)に
  [KERNBASE>>PDXSHIFT] = (0) | PTE_P | PTE_W | PTE_PS,
};

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
