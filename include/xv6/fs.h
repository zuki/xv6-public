// ディスク上のファイルシステムフォーマット。
// カーネルとユーザプログラムは共にこのヘッダファイルを使用する。
#ifndef _XV6_FS_H
#define _XV6_FS_H

#define ROOTINO 1  // ルートinode番号
#define BSIZE 512  // ブロックサイズ

// ディスクレイアウト:
// [ ブートブロック | スーバーブロック | ログ | inodeブロック |
//                                空きビットマップ | データブロック ]
//
// mkfs はスーパーブロックを計算し、初期ファイルシステムを構築する。
// スーパーブロックはディスクレイアウトを記述する:
struct superblock {
  uint size;         // ファイルシステムイメージのサイズ（単位はブロック）
  uint nblocks;      // データブロックの数
  uint ninodes;      // inodeの数
  uint nlog;         // ログブロックの数
  uint logstart;     // 先頭のログブロックのブロック番号
  uint inodestart;   // 先頭のinodeブロックのブロック番号
  uint bmapstart;    // 先頭の空きマップブロックのブロック番号
};

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// オンディスク inode 構造体
struct dinode {
  short type;           // ファイルの種類
  short major;          // メジャーデバイス番号（T_DEV のみ)
  short minor;          // マイナーデバイス番号 (T_DEV のみ)
  short nlink;          // ファイルシステム内のinodeへのリンクの数
  uint size;            // ファイルのサイズ（単位はバイト）
  uint addrs[NDIRECT+1];   // データブロックアドレス
};

// ブロックあたりのinode数
#define IPB           (BSIZE / sizeof(struct dinode))

// inode iを含むブロック
#define IBLOCK(i, sb)     ((i) / IPB + sb.inodestart)

// ブロックあたりのBitmapビット数
#define BPB           (BSIZE*8)

// ビットbを含んでいる空きマップのブロック
#define BBLOCK(b, sb) (b/BPB + sb.bmapstart)

// ディレクトリは一連のdirent構造体を含むファイルである
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

#endif