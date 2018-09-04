#ifndef _XV6_STAT_H
#define _XV6_STAT_H

#define T_DIR  1   // ディレクトリ
#define T_FILE 2   // ファイル
#define T_DEV  3   // デバイス

struct stat {
  short type;  // ファイルの種類
  int dev;     // ファイルシステムのディスク装置
  unsigned int ino;    // inode番号
  short nlink; // ファイルへのリンク数
  unsigned int size;   // ファイルのサイズ（単位はバイト）
};

#endif