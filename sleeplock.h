// プロセス用の長期ロック
struct sleeplock {
  uint locked;       // このロックは保持されているか?
  struct spinlock lk; // このスリープロックを保護するスピンロック

  // デバッグ用:
  char *name;        // ロックの名前
  int pid;           // ロックを保持しているプロセス
};
