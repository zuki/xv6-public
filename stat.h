#define T_DIR  1   // ディレクトリ
#define T_FILE 2   // ファイル
#define T_DEV  3   // デバイス

struct stat {
  short type;  // ファイルの種類
  int dev;     // ファイルシステムのディスクデバイス
  uint ino;    // inode番号
  short nlink; // ファイルへのリンク数
  uint size;   // ファイルのサイズ（単位はバイト）
};
