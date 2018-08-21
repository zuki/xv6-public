// ファイルシステムの実装.  5階層:
//   + ブロック: 生ディスクブロックのアロケータ
//   + ログ: 多段階更新のクラッシュリカバリ
//   + ファイル: inodeアロケータ、読み取り、書き込み、メタデータ
//   + ディレクトリ: 特別なコンテンツ（inodeのリスト）を持つinode
//   + 名前: 便利な命名法である /usr/rtm/xv6/fs.c のようなパス
//
// このファイルには低レベルのファイルシステム操作関数が含まれている。
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
// ディスク装置ごとに１つスーパーブロックが必要であるが、このOSでは１つしかデバイスを
// 使わない。
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
//PAGEBREAK!
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

// ディスクブロックを割り当てて0クリア（ブロック番号を返す）
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
      if((bp->data[bi/8] & m) == 0){  // このブロックは空いているか
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

// inode。
//
// inodeは無名のファイルを一つ記述する。
// inodeディスク構造体はメタデータ（ファイル種別、サイズ、
// 自身を参照しているリンクの数、ファイルコンテンツを保持
// しているブロックのリスト）を保持する。
//
// inodeはディスク上のsb.startinodeから連続的に
// 配置されている。各inodeは番号を持っており、
// これはディスク上の位置を示している。
//
// カーネルは使用中のinodeのキャシュをメモリ上に保持しており、
// これにより複数のプロセスが使用するinodeへの同期的アクセスを
// 実現している。キャッシュされたinodeには、ディスクには保存されない
// 記帳情報(book-keeping information)であるip->refとip->validが
// 含まれている。
//
// inodeとそのインメモリコピーは、ファイルシステムのその他のコードが
// 使用できるようになるまでに、次のように状態が変遷する。
//
// * Allocation(割り当て): inodeはその（ディスク上の）種別が
//   0でない場合、割り当てが行われる。
//   ialloc()は割り当てを行い、iput()は参照とリンクのカウントが0に
//   なったら、解放する。
//
// * Referencing in cache(キャッシュ内で参照中): inodeキャッシュのエントリは
//   ip->refが0になると解放される。そうでない場合、ip->refは
//   そのエントリ（オープンファイルとカレントディレクトリ）への
//   インメモリポインタの数を監視する。
//   iget()は、キャッシュエントリの検出または作成を行い、
//   refをインクリメントする。iput()はrefをデクリメントする。
//
// * Valid(有効): inodeキャッシュエントリの情報（種別、サイズなど）は
//   ip->validが1の時にのみ、正しいものである。
//   ilock()はinodeをディスクから読み込み、ip->validをセットする。
//   iput()はip->refが0になった時に、ip->validをクリアする。
//
// * Locked(ロック): ファイルシステムのコードは、inodeを最初にロックした
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
// ilock()とiget()が分離されているため、システムコールはinodeへの
// 長期的な参照を得る（ファイルのオープンなど）ことができる一方で、
// （read()のように）inodeの短期的なロックのみをすることもできる。
// この分離はパス名検索の際のデッドロックや競合の防止にも役立っている。
//
// iget()はip->refをインクリメントするので、inodeはキャッシュに留まり、
// inodeを指すポイントが引き続き有効となる。
//
// 内部ファイルシステム関数の多くは、使用するinodeが呼び出し側(caller)で
// ロックされていることを想定している。これは呼び出し側に多段階アトミック
// 操作を作成するよう仕向けるためである。
//
// icache.lockスピンロックはicacheエントリの割り当てを保護する。
// ip->refはエントリが空きであるか否かを、ip->devとip->inumは
// エントリがどのinodeを保持しているかを示すので、これらフィールドの
// いずれかを使用する際は、icache.lockを取得する必要がある。
//
// ip->lockスリープロックは、inode構造体のref, dev, inum以外のすべての
// フィールドを保護する。inodeのip->valid, ip->size, ip->type
// などの読み書きをする際は、ip->lockを取得する必要がある。

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
// デバイスdevにinodeを割り当てる。
// inodeの種別をtypeとし、割り当て済みのマークを付ける。
// 未ロックだが割り当て済みかつ参照済みのinodeを返す。
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // 空きinode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // デスクに割り当て済みのマークを付ける
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// 変更したインメモリinodeをディスクにコピーする。
// inodeキャッシュはライトスルーなので、ディスクに存在するinodeの
// 任意のip->xxxフィールドを変更した後に呼び出す必要がある。
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

// デバイスdev上でinode番号inumを持つinodeを探し、
// そのインメモリコピーを返す。inodeはロックせず、
// ディスクからの読み込みもしない。
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
// それが最後の参照だった場合、そのinodeキャッシュエントリは
// リサイクル可能になる。
// それが最後の参照で、そのinodeへのリンクがない場合、
// ディスク上のinode（とそのコンテンツ）を解放する。
// inodeを解放する場合に備えて、iput()の呼び出しは常に
// トランザクション内でなければならない。
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
// 各inodeに関連するコンテンツ（データ）はディスクのブロックに
// 格納される。最初のNDIRECT個のブロック番号は
// ip->addrs[]に記録される。次のNINDIRECT個のブロックは
// ip->addrs[NDIRECT]のブロックに記録される。.

// inode ipのn番目のブロックのディスクブロックアドレスを返す。
// そのようなブロックが存在しない場合、bmapはブロックを割り当てる。
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  uint idx1, idx2;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0)
      ip->addrs[bn] = addr = balloc(ip->dev);
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // 単純間接ブロックをロードする。必要であれば割り当てる。
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
  bn -= NINDIRECT;

  if(bn < NINDIRECT*NINDIRECT){
    // 二重間接ブロックをロードする。必要であれば割り当てる。
    if((addr = ip->addrs[NDIRECT+1]) == 0)
      ip->addrs[NDIRECT+1] = addr = balloc(ip->dev);
    idx1 = bn / 128;  // ip->addrs[NDIRECT+1]内のインデックス
    idx2 = bn % 128;  // ip->addrs[NDIRECT+1][idex1]内のインデックス
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[idx1]) == 0){  // 二重間接の1段階目
      a[idx1] = addr = balloc(ip->dev);
      log_write(bp);
    }
    brelse(bp);
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[idx2]) == 0){  // 二重間接の2段階目
      a[idx2] = addr = balloc(ip->dev);
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
// でない）場合にのみ呼び出される。
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
// 呼び出し側でip->lockを取得しなければならない。
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
// 呼び出し側でip->lockを取得しなければならない。
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
// 呼び出し側でip->lockを取得しなければならない。
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

// ディレクトリでディレクトリエントリを検索する。
// 見つかった場合、エントリのバイトオフセットを *poff に設定する。
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)          // dpがディレクトリでない
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
  if((ip = dirlookup(dp, name, 0)) != 0){  // 存在した
    iput(ip);                              // dirlookupでref++しているのでref--
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
// 呼び出し側が *path=='\0' をチェックして、nameが最終要素であるか
// 否かを判断できるように、返されるパスには先頭にスラッシュを付けない。
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
