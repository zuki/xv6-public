#define NPROC        64  // プロセスの最大数
#define KSTACKSIZE 4096  // 各プロセスのカーネルスタックサイズ
#define NCPU          8  // CPUの最大数
#define NOFILE       16  // プロセス当りのオープンファイル
#define NFILE       100  // システム当りのオープンファイル
#define NINODE       50  // アクティブinodeの最大数
#define NDEV         10  // メジャーデバイス番号の最大値
#define ROOTDEV       1  // ルートディスクファイルシステムのデバイス番号
#define MAXARG       32  // execの引数の最大数
#define MAXOPBLOCKS  10  // FS操作関数が書き込み可能な最大ブロック数
#define LOGSIZE      (MAXOPBLOCKS*3)  // オンディスクログの最大データブロック
#define NBUF         (MAXOPBLOCKS*3)  // ディスクブロックキャッシュのサイズ
#define FSSIZE    40000  // ファイルシステムのサイズ（単位はブロック）
