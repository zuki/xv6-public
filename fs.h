// オンディスク・ファイルシステムフォーマット。
// カーネルとユーザプログラムは共にこのヘッダファイルを使用する。


#define ROOTINO 1  // ルートinode番号
#define BSIZE 512  // ブロックサイズ

// ディスクレイアウト:
// [ boot block | super block | log | inode blocks |
//                                          free bit map | data blocks]
//
// mkfs はスーパーブロックを計算し、初期ファイルシステムを構築する。
// スーパーブロックはディスクレイアウトを記述する:
struct superblock {
  uint size;         // ファイルシステムイメージのサイズ（単位はブロック）
  uint nblocks;      // データブロックの数
  uint ninodes;      // inodeの数
  uint nlog;         // ログブロックの数
  uint logstart;     // ログブロックの最初のブロック番号
  uint inodestart;   // inodeブロックの最初のブロック番号
  uint bmapstart;    // 空きマップブロックの最初のブロック番号
};

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// オンディスク inode 構造体
struct dinode {
  short type;           // ファイルのタイプ
  short major;          // メジャーデバイス番号（T_DEV のみ)
  short minor;          // マイナーデバイス番号 (T_DEV のみ)
  short nlink;          // ファイルシステムのinodeへのリンクの数
  uint size;            // ファイルのサイズ（単位はバイト）
  uint addrs[NDIRECT+1];   // データブロックアドレス
};

// ブロックあたりのinode
#define IPB           (BSIZE / sizeof(struct dinode))

// inode iを含むブロック
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// ブロックあたりのBitmapビット数
#define BPB           (BSIZE*8)

// ブロックbのbitを含む空きマップのブロック
#define BBLOCK(b, sb) (b/BPB + sb.bmapstart)

// ディレクトリは一連のdirent構造体を含むファイルである
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};
