// ファイルシステムの実装.  ５つの層:
//   + ブロック: 生のディスクブロックのアロケータ
//   + ログ: 複数ステップによる更新のクラッシュリカバリ
//   + ファイル: inodeアロケータ、読み、書き、メタデータ
//   + ディレクトリ: 特別なコンテンツ（他のinodeのリスト）を持つinode
//   + 名前: 便利な命名方法としての /usr/rtm/xv6/fs.c のようなパス
//
// このファイルは低レベルのファイルシステム操作ルーチンを含んでいる。
// （高レベルの）システムコールの実装はsysfile.cにある。

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
static void itrunc(struct inode*);
// ディスクデバイスごとに１つスーパーブロックが必要であるが、このOSで使えるデバイスは
// 1つだけ。
struct superblock sb;

// スーパーブロックを読み込む
void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// ブロックを0クリア
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// ブロック

// ディスクブロックを割り当てて0クリア
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // ブロックは空き?
        bp->data[bi/8] |= m;  // ブロックに使用中のマークを付ける。
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}

// ディスクブロックを解放する
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  readsb(dev, &sb);
  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}

// Inode.
//
// inodeは名前のない１つのファイルを記述する。
// inodeディスク構造体はメタデータ（ファイル種別、
// サイズ、自身を参照しているリンクの数、ファイルコンテンツを保持している
// ブロックのリスト）を保持する。
//
// inodeはディスク上にsb.startinodeから連続的に配置されている。
// 各inodeはディスク上の位置を示す番号を持っている。
//
// カーネルは使用中のinodeのキャシュをメモリ上に保持しており、
// これにより複数プロセスの使用によるinodeへの同期的アクセスを
// 実現している。キャッシュされたinodeには、ディスクには保管されない
// 記帳情報(book-keeping information): ip->refとip->valid
// が含まれている。
//
// inodeとそのインメモリコピーは、他のファイルシステムコードで
// 使用可能になるまでに、次のように状態が変遷する。
//
// * Allocation(割当): inodeはその（ディスク上の）種別が0でない場合、
//   割当が行われる。
//   ialloc()が割当を行い、参照とリンクカウントが0になった場合に
//   iput()が解放する。
//
// * Referencing in cache(キャッシュ内で参照中): inodeキャッシュのエントリは
//   ip->refが0になると解放される。そうでない場合、ip->refは
//   そのエントリ（オープンファイルとカレントディレクトリ）への
//   インメモリポインタの数を監視する。
//   iget()は、キャッシュエントリの検出または作成を行い、
//   そのrefをインクリメントする。iput()はrefをデクリメントする。
//
// * Valid(有効): inodeキャッシュエントリの情報（種別、サイズ、&c）は
//   ip->validが1の時にのみ、正しいものである。
//   ilock()はinodeをディスクから読み込み、ip->validをセットする。
//   iput()はip->refが0になった時に、ip->validをクリアする。
//
// * Locked(ロック): ファイルシステムコードは、まずinodeをロックした
//   場合にのみ、icode内の情報やそのコンテンツを調べたり、変更したり
//   することができる。
//
// したがって、典型的なコードの流れは次のようになる:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... ip->xxxのチェックと変更 ...
//   iunlock(ip)
//   iput(ip)
//
// ilock()とiget()から分離されているため、システムコールはinodeへの長期参照を得る
// （ファイルのオープンなど）ことができ、一方、（read()のように）短期間のみ
// inodeをロックすることもできる。
// この分離はパス名検索の際のデッドロックや競合の防止にも役立っている。
// iget()はip->refをインクリメントので、
// inodeはキャッシュに留まり、inodeを指すポイントが引き続き有効と
// なる。
//
// 内部ファイルシステム関数の多くは、使用するinodeが呼び出し側(caller)で
// ロックされていることを想定している。これは呼び出し側が複数ステップの
// アトミック操作を作成するよう仕向けるためである。
//
// icache.lockスピンロックはicacheエントリの割当を保護する。
// ip->refはエントリが空いているか否かを、ip->devとip->inumは
// エントリがどのinodeを保持しているかを示すので、これらのフィールドの
// いずれかを使用する際は、icache.lockを保持する必要がある。
//
// ip->lockスリープロックは、ip->のref, dev, inum以外のすべての
// フィールドを保護する。inodeのip->valid, ip->size, ip->type
// などの読み書きをするには、ip->lockを保持する必要がある。

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

void
iinit(int dev)
{
  int i = 0;

  initlock(&icache.lock, "icache");
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&icache.inode[i].lock, "inode");
  }

  readsb(dev, &sb);
  cprintf("sb: size %d nblocks %d ninodes %d nlog %d logstart %d\
 inodestart %d bmap start %d\n", sb.size, sb.nblocks,
          sb.ninodes, sb.nlog, sb.logstart, sb.inodestart,
          sb.bmapstart);
}

static struct inode* iget(uint dev, uint inum);

//PAGEBREAK!
// デバイス devにinodeを割り当てる。
// 指定されたタイプ typeのinodeであるとマーク付ける。
// ロックされていない、割り当て済みで参照済みのinodeを返す。
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // デスクに割り当て済みであるとマーク付ける
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// 変更したインメモリinodeをディスクにコピーする。
// inodeキャッシュはライトスルーなので、ディスクに存在するinodeの
// ip->xxxのフィールドを変更する度によびだされなければならない。
// 呼び出し側はip->lockを保持しなければならない。
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// デバイス devでinode番号 inumを持つinodeを探し、
// そのインメモリコピーを返す。inodeはロックせず、
// ディスクから読み込みもしない。
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&icache.lock);

  // 対象のinodeはすでにキャッシュされているか？
  empty = 0;
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // 空のスロットを記憶する
      empty = ip;
  }

  // inodeキャッシュエントリをリサイクルする
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&icache.lock);

  return ip;
}

// ipの参照カウントをインクリメントする。
// イディオム ip = idup(ip1) を使えるように、ipを返す。
struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// 指定されたinodeをロックする。
// 必要であれば、ディスクからinodeを読み込む。
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

// 指定されたinodeのロックを外す
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

// インメモリinodeへの参照をデクリメントする。
// それが最後の参照だった場合は、inodeキャッシュエントリがリサイクル可能になる。
// それが最後の参照で、それへのリンクがない場合は、ディスク上のinode（とその
// コンテンツ）を解放する。
// inodeを解放する必要がある場合に備えて、iput()の呼び出しはすべて
// トランザクションの内側で行われなければならない。
void
iput(struct inode *ip)
{
  acquiresleep(&ip->lock);
  if(ip->valid && ip->nlink == 0){
    acquire(&icache.lock);
    int r = ip->ref;
    release(&icache.lock);
    if(r == 1){
      // inodeはリンクがなく、他に参照もない: 切り詰めて解放する
      itrunc(ip);
      ip->type = 0;
      iupdate(ip);
      ip->valid = 0;
    }
  }
  releasesleep(&ip->lock);

  acquire(&icache.lock);
  ip->ref--;
  release(&icache.lock);
}

// 一般的なイディオム: unlockしてputする
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

//PAGEBREAK!
// inodeのコンテンツ
//
// 各inodeに関係するコンテンツ（データ）はディスクのブロックに
// 格納される。最初のNDIRECT個のブロック番号は
// ip->addrs[]に記録される。次のNINDIRECTこのブロックは
// ip->addrs[NDIRECT]のブロックに記録される。.

// inode ipのn番目のブロックのディスクブロックアドレスを返す。
// そのようなブロックが存在しない場合、bmapはブロックを割り当てる。
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // 間接ブロックを、必要なら割り当てて、ロードする。
    if((addr = ip->addrs[NDIRECT]) == 0)
      ip->addrs[NDIRECT] = addr = balloc(ip->dev);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      a[bn] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

// inodeを切り詰める（コンテンツを破棄する）。
// そのinodeへのリンクがなく（参照するディレクトリ
// エントリがない）、かつ、そのinodeへのインメモリ参照が
// ない（オープンされたファイルでないか、カレントディレクトリ
// でない）場合にのみ呼び出される
static void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

// inodeからstat情報をコピーする。
// 呼び出し側でip->lockを保持しなければならない。
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

//PAGEBREAK!
// inodeからデータを読み込む。
// 呼び出し側でip->lockを保持しなければならない。
int
readi(struct inode *ip, char *dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
      return -1;
    return devsw[ip->major].read(ip, dst, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(dst, bp->data + off%BSIZE, m);
    brelse(bp);
  }
  return n;
}

// PAGEBREAK!
// データをinodeに書き込む。
// 呼び出し側でip->lockを保持しなければならない。
int
writei(struct inode *ip, char *src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write)
      return -1;
    return devsw[ip->major].write(ip, src, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    bp = bread(ip->dev, bmap(ip, off/BSIZE));
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(bp->data + off%BSIZE, src, m);
    log_write(bp);
    brelse(bp);
  }

  if(n > 0 && off > ip->size){
    ip->size = off;
    iupdate(ip);
  }
  return n;
}

//PAGEBREAK!
// ディレクトリ

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// ディレクトリ中のディレクトリエントリを検索する。
// 見つかった場合、エントリのバイトオフセットを *poff に設定する。
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)          // dpがディレクトリでない場合はエラー
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0)
      continue;
    if(namecmp(name, de.name) == 0){
      // エントリがパス要素に一致
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

// 新しいディレクトリエントリ（名前、inum）をディレクトリdpに書き込む。
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // 名前が存在しないかチェックする。
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // 空のdirentを探す。
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}

//PAGEBREAK!
// パス

// pathから次のパス要素をnameにコピーする。
// コピーした要素の次の要素へのポインタを返す。
// 呼び出し側がnameが最終要素であるか否かを *path=='\0' で
// チェックできるように、返されるパスには先頭にスラッシュを付けない。
// 取り除く名前がなかった場合は、0 を返す。
//
// 例:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// パス名を検索して、そのinodeを返す
// nameiparent != 0の場合は、親のinodeを返し、最終パス要素をnameにコピーする。
// nameはDIRSIZ(14)バイトが確保されていなければならない。
// iput()を呼び出すので、トランザクションの内側で呼び出す必要がある。
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;

  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(myproc()->cwd);

  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // １レベル前で処理を停止
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}
