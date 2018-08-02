// I/O APICはSMPシステムのハードウェア割り込みを管理する。
// http://www.intel.com/design/chipsets/datashts/29056601.pdf
// picirq.cも参照のこと。

#include "types.h"
#include "defs.h"
#include "traps.h"

#define IOAPIC  0xFEC00000   // IO APICのデフォルト物理アドレス

#define REG_ID     0x00  // レジスタインデックス: ID
#define REG_VER    0x01  // レジスタインデックス: バージョン
#define REG_TABLE  0x10  // リダイレクションテーブルベース

// リダイレクションテーブルはREG_TABLEから始まり、
// 2つのレジスタを使って、各割り込みを構成する。
// 最初の（低位）レジスタは構成ビットを持つ。
// 2番めの（高位）レジスタは、どのCPUがその割り込みに対応できるかを
// 示すビットマスクを持つ。
#define INT_DISABLED   0x00010000  // 割り込みは禁止
#define INT_LEVEL      0x00008000  // レベルトリガ（vs エッジトリガ）
#define INT_ACTIVELOW  0x00002000  // アクティブロー（vs アクティブハイ）
#define INT_LOGICAL    0x00000800  // 宛先はCPU id（vs APIC ID）

volatile struct ioapic *ioapic;

// IO APIC MMIO 構造体: regに書き込み、次にdataの読み込み/書き込みを行う。
struct ioapic {
  uint reg;
  uint pad[3];
  uint data;
};

static uint
ioapicread(int reg)
{
  ioapic->reg = reg;
  return ioapic->data;
}

static void
ioapicwrite(int reg, uint data)
{
  ioapic->reg = reg;
  ioapic->data = data;
}

void
ioapicinit(void)
{
  int i, id, maxintr;

  ioapic = (volatile struct ioapic*)IOAPIC;
  maxintr = (ioapicread(REG_VER) >> 16) & 0xFF;
  id = ioapicread(REG_ID) >> 24;
  if(id != ioapicid)
    cprintf("ioapicinit: id isn't equal to ioapicid; not a MP\n");

  // すべての割り込みの設定を、エッジトリガ、アクティブハイ、割り込み無効、
  // どのCPUにも転送しない、とする。
  for(i = 0; i <= maxintr; i++){
    ioapicwrite(REG_TABLE+2*i, INT_DISABLED | (T_IRQ0 + i));
    ioapicwrite(REG_TABLE+2*i+1, 0);
  }
}

void
ioapicenable(int irq, int cpunum)
{
  // 割り込みの設定を、エッジトリガ、アクティブハイ、割り込み有効、
  // 指定されたcpunumに転送する、とする。
  // cpunumはそのcpuのAPIC IDでもある。
  ioapicwrite(REG_TABLE+2*irq, T_IRQ0 + irq);
  ioapicwrite(REG_TABLE+2*irq+1, cpunum << 24);
}
