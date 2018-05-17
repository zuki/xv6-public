// プロセス用の長期間ロック
struct sleeplock {
  uint locked;       // このロックは獲得済みか?
  struct spinlock lk; // このスリープロックを保護するためのスピンロック

  // デバッグ用:
  char *name;        // ロックの名前
  int pid;           // ロックを獲得中のプロセス
};
