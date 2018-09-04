// 相互排他ロック
#ifndef _XV6_SPINLOCK_H
#define _XV6_SPINLOCK_H


struct spinlock {
  uint locked;       // このロックは獲得済みか?

  // デバック用:
  char *name;        // ロックの名前
  struct cpu *cpu;   // ロックを保持しているCPU
  uint pcs[10];      // このロックをロックしたコールスタック
                     // （プログラムカウンタの配列）
};

#endif