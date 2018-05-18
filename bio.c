// バッファキャッシュ。
//
// バッファキャッシュは、ディスクブロックコンテンツのキャッシュコピーを
// 保持するバッファ構造体の連結リストである。ディスクブロックをメモリにキャッシュすることは
// ディスクリードの回数を減らす共に、マルチプロセスにより使用される
// ディスクブロックに同期点を与える。
//
// インターフェース:
// * 特定のディスクブロックを取得するために、breadを呼び出す。
// * バッファデータをキャッシュした後、データをディスクに書き込みにはbwriteを呼び出す。
// * バッファを使う作業が終わったら、brelseを呼び出す。
// * brelseを呼び出した後には、バッファを使用しない。
// * バッファを使用できるのは一度に1プロセスだけである。
//     そのため、必要以上にバッファを保持しないこと。
//
// この実装では内部で次の2つの状態フラグを使用する。
// * B_VALID: バッファデータはディスクから読み込まれている。
// * B_DIRTY: バッファデータは変更されており、
//     ディスクに書き込む必要がある。

#include "types.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // prev/nextでたどることができる、すべてのバッファの連結リスト。
  // head.nextはもっとも最近使用されたバッファである。
  struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

//PAGEBREAK!
  // バッファの連結リストを作成する。
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// バッファキャッシュを走査して、デバイスdevのブロックを探し出す。
// 見つからなかった場合は、バッファを割り当てる。
// どちらの場合も、ロックしたバッファを返す。
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // バッファはキャシュ済みか?
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // キャッシュされていいない。未使用のバッファをリサイクルする。
  // Even if refcnt==0であっても、B_DIRTYの場合は、バッファが使用中であることを示す。
  // log.cがそれを変更したが、まだコミットしていないからである。
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0 && (b->flags & B_DIRTY) == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->flags = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// 指定したブロックのコンテンツを含むバッファをロックして返す。
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if((b->flags & B_VALID) == 0) {
    iderw(b);
  }
  return b;
}

// bのコンテンツをディスクに書き込む。bはロックされていなければならない。
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  b->flags |= B_DIRTY;
  iderw(b);
}

// ロックされたバッファを解放する。
// MRUリストのヘッドを移動する。
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // このバッファを待っているものはいない。
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }

  release(&bcache.lock);
}
//PAGEBREAK!
// Blank page.
