struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE } type;
  int ref; // 参照カウント
  char readable;
  char writable;
  struct pipe *pipe;
  struct inode *ip;
  uint off;
};


// inodeのインメモリコピー
struct inode {
  uint dev;           // デバイス番号
  uint inum;          // inode番号
  int ref;            // 参照カウント
  struct sleeplock lock; // 以下のすべてを保護する
  int valid;          // inodeはディスクから読み込まれているか?

  short type;         // ディスクinodeのコピー
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};

// メジャーデバイス番号をデバイス関数に
// マッピングするテーブル
struct devsw {
  int (*read)(struct inode*, char*, int);
  int (*write)(struct inode*, char*, int);
};

extern struct devsw devsw[];

#define CONSOLE 1
