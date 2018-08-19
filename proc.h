// CPU毎のステート
struct cpu {
  uchar apicid;                // ローカルAPIC ID
  struct context *scheduler;   // スケジューラに入るにはここにswtch()
  struct taskstate ts;         // 割り込み用のスタックを探すためにx86が使用
  struct segdesc gdt[NSEGS];   // x86グローバルディスクリプタテーブル
  volatile uint started;       // CPUは開始しているか?
  int ncli;                    // pushcliネストの深さ
  int intena;                  // pushcliの前に割り込みは有効だったか?
  struct proc *proc;           // このCPUで実行中のプロセス。なければnull
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// カーネルコンテキストスイッチで保存されるレジスタ
// すべてのセグメントレジスタ（%csなど）はカーネルコンテキストを
// 通じて不変であるため保存する必要はない。
// %eax, %ecx, %edxは、x86の規約で呼び出し元が保存するので、
// 保存する必要はない。
// コンテキストは自身が記述するスタックの底に格納される。
// すなわち、スタックポインタはコンテキストのアドレスである。
// コンテキストのレイアウトは、switch.Sのコメント"スタックを切り替える"に
// 書かれているスタックのレイアウトに一致する。スイッチはeipを明示的には
// 保存しないが、スタック上にあり、allocproc()がそれを処理する。
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// プロセスごとのステート
struct proc {
  uint sz;                     // プロセスメモリのサイズ（単位はバイト）
  pde_t* pgdir;                // ページテーブル
  char *kstack;                // このプロセスのカーネルスタックの底
  enum procstate state;        // プロセスの状態
  int pid;                     // プロセスID
  struct proc *parent;         // 親プロセス
  struct trapframe *tf;        // 現在のsyscallのトラップフレーム
  struct context *context;     // プロセスの実行するためにここにswtch()
  void *chan;                  // 非ゼロの場合、chanでスリープ中
  int killed;                  // 非ゼロの場合、キルされた
  struct file *ofile[NOFILE];  // オープンしたファイル
  struct inode *cwd;           // カレントディレクトリ
  char name[16];               // プロセス名（デバッグ用）
  int cputicks;                // cpu ticks
  int alarmticks;              // アラーム間隔(ticsk)
  void (*alarmhandler)();      // アラームハンドラ関数
};

// プロセスメモリは、低位アドレスから次のように、連続的に配置される。
//   テキスト
//   オリジナルのデータとbss
//   固定サイズのスタック
//   拡張可能なヒープ
