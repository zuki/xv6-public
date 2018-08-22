#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// 並列FSシステムコールを可能にするシンプルなロギングシステム。
//
// ログトランザクションには複数のFSシステムコールの更新内容が含まれている。
// ロギングシステムはアクティブなFSシステムコールが存在しない場合にのみ
// コミットを行う。したがって、コミットによりコミットされていない
// システムコールの更新がディスクに書き込まれないかと心配する必要は
// まったくない。
//
// システムコールはその開始と終了を知らせるためにbegin_op()/end_op()を
// 呼び出さなければならない。通常、begin_op()は実行中のFSシステムコールの
// カウントをインクリメントしただけで復帰する。
// ただし、ログの枯渇が近いと判断した場合、最後の未処理のend_op()が
// コミットされるまでbegin_op()はスリープする。
//
// ログはディスクブロックを含んでいる物理的なre-doログである。
// オンディスクログフォーマットは次の通り:
//   ヘッダブロック。ブロックA, B, C, ...のブロック番号を含んでいる
//   ブロックA
//   ブロックB
//   ブロックC
//   ...
// ログの追加は同期的に行われる

// ヘッダブロックの内容。オンディスクヘッダブロックと
// コミット前のログ出力ブロック番号をメモリ上で追跡するために使用される
struct logheader {
  int n;
  int block[LOGSIZE];
};

struct log {
  struct spinlock lock;
  int start;
  int size;
  int outstanding; // 実行中のFSシステムコールの数
  int committing;  // commit()中、待て
  int dev;
  struct logheader lh;
};
struct log log;

static void recover_from_log(void);
static void commit();

void
initlog(int dev)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: too big logheader");

  struct superblock sb;
  initlock(&log.lock, "log");
  readsb(dev, &sb);
  log.start = sb.logstart;
  log.size = sb.nlog;
  log.dev = dev;
  recover_from_log();
}

// コミットしたブロックをログからその本来の場所にコピーする。
static void
install_trans(int copy)
{
  int tail;
  struct buf *dbuf, *lbuf;

  for (tail = 0; tail < log.lh.n; tail++) {
    dbuf = bread(log.dev, log.lh.block[tail]); // dstを読み込む
    if (copy) {
      lbuf = bread(log.dev, log.start+tail+1); // ログブロックを読み込む
      memmove(dbuf->data, lbuf->data, BSIZE);  // ブロックをdstにコピーする
    }
    bwrite(dbuf);  // dstからディスクに書き込む
    if (copy)
      brelse(lbuf);
    brelse(dbuf);
  }
}

// ログヘッダをディスクからインメモリログヘッダに読み込む
static void
read_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *lh = (struct logheader *) (buf->data);
  int i;
  log.lh.n = lh->n;
  for (i = 0; i < log.lh.n; i++) {
    log.lh.block[i] = lh->block[i];
  }
  brelse(buf);
}

// インメモリログヘッダをディスクに書き込む。
// ここで本当にカレントトランザクションが
// コミットされることになる。
static void
write_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *) (buf->data);
  int i;
  hb->n = log.lh.n;
  for (i = 0; i < log.lh.n; i++) {
    hb->block[i] = log.lh.block[i];
  }
  bwrite(buf);
  brelse(buf);
}

static void
recover_from_log(void)
{
  read_head();
  install_trans(1); // コミットされていたら、ログからディスクにコピーする
  log.lh.n = 0;
  write_head(); // ログをクリア
}

// 各FSシステムコールの最初に呼び出される。
void
begin_op(void)
{
  acquire(&log.lock);
  while(1){
    if(log.committing){
      sleep(&log, &log.lock);
    } else if(log.lh.n + (log.outstanding+1)*MAXOPBLOCKS > LOGSIZE){
      // この操作によりログスペースが枯渇するおそれがあるので、コミットされるまで待機する。
      sleep(&log, &log.lock);
    } else {
      log.outstanding += 1;
      release(&log.lock);
      break;
    }
  }
}

// 各FSシステムコールの最後に呼び出される。
// これが最後の未処理の操作だった場合はコミットする。
void
end_op(void)
{
  int do_commit = 0;

  acquire(&log.lock);
  log.outstanding -= 1;
  if(log.committing)
    panic("log.committing");
  if(log.outstanding == 0){
    do_commit = 1;
    log.committing = 1;
  } else {
    // begin_op()がログスペースが空くのを待っている可能性がある。
    // log.outstandingをデクリメントしたことで予約スペースが
    // 減ったかもしれない。
    wakeup(&log);
  }
  release(&log.lock);

  if(do_commit){
    // ロックを保持したままスリープすることは許されないので、
    // ロックを獲得せずにcommitを呼び出す。
    commit();
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }
}

// 変更されたブロックをキャッシュからログにコピーする。
static void
write_log(void)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++) {
    struct buf *to = bread(log.dev, log.start+tail+1); // ログブロック
    struct buf *from = bread(log.dev, log.lh.block[tail]); // キャッシュブロック
    memmove(to->data, from->data, BSIZE);
    bwrite(to);  // ログを書き込む
    brelse(from);
    brelse(to);
  }
}

static void
commit()
{
  if (log.lh.n > 0) {
    write_log();     // 変更されたブロックをキャッシュからログに書き込む
    write_head();    // ヘッダをディスクに書き込む -- 本当のコミット
    install_trans(0); // 書き込み内容を本来の場所にコピーする
    log.lh.n = 0;
    write_head();    // トランザクションをログから消去する
  }
}

// 呼び出し側でb->dataを変更した。それは、バッファ上で行われている。
// ブロック番号を記録して、キャッシュのB_DIRTYフラグをたてる。
// いずれ、commit()/write_log()がディスクへの書き込みを行う。
//
// log_write()はbwrite()の代わりに使用する。通常の使用法は次の通り:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void
log_write(struct buf *b)
{
  int i;

  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  acquire(&log.lock);
  for (i = 0; i < log.lh.n; i++) {
    if (log.lh.block[i] == b->blockno)   // ログの統合
      break;
  }
  log.lh.block[i] = b->blockno;
  if (i == log.lh.n)
    log.lh.n++;
  b->flags |= B_DIRTY; // スワップアウトを防ぐ
  release(&log.lock);
}
