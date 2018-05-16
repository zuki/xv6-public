// 相互排他ロック
struct spinlock {
  uint locked;       // このロックは獲得済みか?

  // デバック用:
  char *name;        // ロックの名前
  struct cpu *cpu;   // ロックを獲得中のCPU
  uint pcs[10];      // このロックをロックしたコールスタック
                     // （プログラムカウンタの配列）
};
