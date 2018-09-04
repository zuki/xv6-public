#include <xv6/param.h>
#include <sys/types.h>
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // kernel.ldで定義
pde_t *kpgdir;  // scheduler()で使用される

// CPUのカーネルセグメントディスクリプタを設定する。
// 各CPUのentryで1回ずつ実行される。
void
seginit(void)
{
  struct cpu *c;

  // 恒等マッピングを使用して「論理」アドレスを仮想アドレスにマッピングする。
  // カーネル用とユーザ用のコードディスクリプタの共用はできない。
  // なぜなら、共用する場合はDPL_USRを持たねばならないが、CPUはCPL=0から
  // DPL=3への割り込みを禁止しているからである。
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// 仮想アドレスvaに対応するページテーブルpgdirのPTEのアドレスを返す。
// alloc != 0 の場合は、
// 必要なページテーブルページを作成する。
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0) // 割当不要または失敗の場合は0を返す
      return 0;
    // すべてのページテーブルエントリのPTE_Pビットを0にする
    memset(pgtab, 0, PGSIZE);
    // 通常、ここでのパーミッションは上書きされるが、
    // 必要であれば、ページテーブルエントリのパーミションでさらに制限する
    // こともできる。
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// vaから始まる仮想アドレスがpaから始まる物理アドレスを参照するように
// するためのPTE（ページテーブルエントリ）を作成する。
// vaとサイズはページ境界にない可能性がある。
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// ページテーブルはプロセスごとに1つあり、さらに1つ、CPUがプロセスを
// 1つも実行していない時に使用するページテーブルがある(kpgdir)。
// システムコールと割り込み処理の際、カーネルはカレントプロセスの
// ページテーブルを使用する。ページ保護ビットがユーザコードによる
// カーネルマッピングの使用を禁止する。
//
// setupkvm() と exec() はすべてのページテーブルを次のように設定する:
//
//   0..KERNBASE: ユーザメモリ (text+data+stack+heap)
//                カーネルにより割り当てられる物理メモリにマッピングされる
//   KERNBASE..KERNBASE+EXTMEM: 0..EXTMEM にマッピングされる(I/O 空間用)
//   KERNBASE+EXTMEM..data: EXTMEM..V2P(data)にマッピングされる
//                カーネルの命令コードと読み込み専用データ用
//   data..KERNBASE+PHYSTOP: V2P(data)..PHYSTOPにマッピングされる
//                読み書きデータと空き物理メモリ
//   0xfe000000..0: そのままマッピングされる（ioapicなどのデバイス）
//
// カーネルは自身のヒープ用とユーザメモリ用の物理メモリを
// V2P(end)から物理メモリ上限(PHYSTOP)の間に割り当てる。
// （end..P2V(PHYSTOP)は直接アドレスすることができる）

// このテーブルはカーネルのマッピングを定義する。これは全プロセスの
// ページテーブルに現れる
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O空間
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // カーネルのtext+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // カーネルのdata+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // デバイス
};

// ページテーブルのカーネル部分を設定する
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)    // ページテーブルの割り当て
    return 0;
  memset(pgdir, 0, PGSIZE);              // ページテーブルを0詰め
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// スケジューラプロセスが使用するカーネルアドレス空間用の
// ページテーブルをマシンに1つ割り当てる
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// 実行中のプロセスがない場合は、ハードウェアのページテーブルレジスタを
// カーネル専用のページテーブルに切り替える
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // カーネルページテーブルに切り替える
}

// TSSとハードウェアページテーブルをプロセスpのものに切り替える
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;       // TSSディスクリプタ
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // eflagsにおけるIOPL=0の設定、*かつ*、iombへのtssセグメントのリミット値を
  // 超える値の設定により、ユーザ空間からのI/O命令（inbとoutbなど）を禁止する。
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // プロセスのアドレス空間に切り替える
  popcli();
}

// initcodeをpgdirのアドレス0にロードする。
// sz は1ページ未満でなければならない。
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();  // 4096byte = 1 pageを割り当て
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// プログラムセグメントをpgdirにロードする。addrはページ境界になければならない。
// また、addrからaddr+szのページはマッピング済みでなければならない。
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// プロセスをoldszからnewszに拡張するためにページテーブルと物理メモリを割り当てる。
// サイズはページ境界になくても良い。新しいサイズを返す。エラーの場合は0を返す。
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

// プロセスサイズをoldszからnewszにするためにユーザページの割り当てを解除する。
// oldszとnewszはページ境界になくても良い。また、newszはoldszより小さくなくても良い。
// oldszは実際のプロセスサイズより大きくても良い
// 新しいプロセスサイズを返す
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// ページテーブルとユーザ部分のすべての物理メモリを
// 解放する。
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// ページのPTE_Uをクリアする。ユーザスタック直下に
// アクセス不能なページを作成するのに使用する
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// 親プロセスのページテーブルを与え、子プロセス用に
// それのコピーを作成する。
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0)
      goto bad;
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// ユーザ仮想アドレスをカーネルアドレスにマッピングする。
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// pからページテーブル pgdirのユーザアドレス va へ lenバイトコピーする。
// pgdirがカレントページテーブルでない場合に最も役に立つ。
// uva2ka はこの関数がPTF_Uページでのみ動作することを保証する。
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
