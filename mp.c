// マルチプロセッササポート
// MP記述構造体をメモリ上で探索する。
// http://developer.intel.com/design/pentium/datashts/24201606.pdf

#include <sys/types.h>
#include "defs.h"
#include <xv6/param.h>
#include "memlayout.h"
#include "mp.h"
#include "x86.h"
#include "mmu.h"
#include "proc.h"

struct cpu cpus[NCPU];
int ncpu;
uchar ioapicid;

static uchar
sum(uchar *addr, int len)
{
  int i, sum;

  sum = 0;
  for(i=0; i<len; i++)   // int(32bit)で計算してuchar(8bit)で返す。
    sum += addr[i];      // checksumとしては最下位1バイトが0であれば良い
  return sum;
}

// addrからlenバイト内でMP構造体を探索する。
static struct mp*
mpsearch1(uint a, int len)
{
  uchar *e, *p, *addr;

  addr = P2V(a);
  e = addr+len;
  for(p = addr; p < e; p += sizeof(struct mp))
    if(memcmp(p, "_MP_", 4) == 0 && sum(p, sizeof(struct mp)) == 0)
      return (struct mp*)p;
  return 0;
}

// MP浮動ポインタ構造体を探索する。
// 仕様書によれば次の3箇所のいずれかにある。
// 1) EBDAの最初のKB内;
// 2) システムベースメモリの最後のKB内;
// 3) BIOS ROMの0xF0000から0xFFFFFの間。（訳注: 仕様書には0xF0000とあるのでtypoだろう）
static struct mp*
mpsearch(void)
{
  uchar *bda;
  uint p;
  struct mp *mp;

  bda = (uchar *) P2V(0x400);
  if((p = ((bda[0x0F]<<8)| bda[0x0E]) << 4)){
    if((mp = mpsearch1(p, 1024)))
      return mp;
  } else {
    p = ((bda[0x14]<<8)|bda[0x13])*1024;
    if((mp = mpsearch1(p-1024, 1024)))
      return mp;
  }
  return mpsearch1(0xF0000, 0x10000);
}

// MP構成テーブルを探索する。さしあたり、デフォルトの
// 構成（physaddr == 0）は受け付けない。
// シグネチャが正しいかチェックし、チェックサムを計算する。
// 正しければ、バージョンをチェックする。
// TODO: 拡張テーブルチェックサムをチェックする。
static struct mpconf*
mpconfig(struct mp **pmp)
{
  struct mpconf *conf;
  struct mp *mp;

  if((mp = mpsearch()) == 0 || mp->physaddr == 0)
    return 0;
  conf = (struct mpconf*) P2V((uint) mp->physaddr);
  if(memcmp(conf, "PCMP", 4) != 0)
    return 0;
  if(conf->version != 1 && conf->version != 4)
    return 0;
  if(sum((uchar*)conf, conf->length) != 0)
    return 0;
  *pmp = mp;
  return conf;
}

void
mpinit(void)
{
  uchar *p, *e;
  int ismp;
  struct mp *mp;
  struct mpconf *conf;
  struct mpproc *proc;
  struct mpioapic *ioapic;

  if((conf = mpconfig(&mp)) == 0)
    panic("Expect to run on an SMP");
  ismp = 1;
  lapic = (uint*)conf->lapicaddr;
  for(p=(uchar*)(conf+1), e=(uchar*)conf+conf->length; p<e; ){
    switch(*p){
    case MPPROC:
      proc = (struct mpproc*)p;
      if(ncpu < NCPU) {
        cpus[ncpu].apicid = proc->apicid;  // apicid はncpuとは異なる可能性あり
        ncpu++;
      }
      p += sizeof(struct mpproc);
      continue;
    case MPIOAPIC:
      ioapic = (struct mpioapic*)p;
      ioapicid = ioapic->apicno;
      p += sizeof(struct mpioapic);
      continue;
    case MPBUS:
    case MPIOINTR:
    case MPLINTR:
      p += 8;
      continue;
    default:
      ismp = 0;
      break;
    }
  }
  if(!ismp)
    panic("Didn't find a suitable machine");

  if(mp->imcrp){
    // BochsはIMCRをサポートしていない。そのため、Bochs上では動かない。
    // しかし、実際のマシン上では動くはず。
    outb(0x22, 0x70);   // IMCRを選択
    outb(0x23, inb(0x23) | 1);  // 外部割り込みをマスクする
  }
}
