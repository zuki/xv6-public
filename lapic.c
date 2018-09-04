// ローカルAPICは内部（非I/O）割り込みを管理する。
// Intelプロセッサマニュアル第3巻の8章と付録Cを参照のこと。

#include <xv6/param.h>
#include "types.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "traps.h"
#include "mmu.h"
#include "x86.h"

// ローカルAPICレジスタ、unit[]のインデックスとして使うために4で割っている。
#define ID      (0x0020/4)   // ID
#define VER     (0x0030/4)   // バージョン
#define TPR     (0x0080/4)   // タスク優先度
#define EOI     (0x00B0/4)   // EOI
#define SVR     (0x00F0/4)   // スプリアス割り込みベクタ
  #define ENABLE     0x00000100   // ユニットイネーブル
#define ESR     (0x0280/4)   // エラーステータス
#define ICRLO   (0x0300/4)   // 割り込みコマンド
  #define INIT       0x00000500   // INIT/RESET
  #define STARTUP    0x00000600   // スタートアップIPI
  #define DELIVS     0x00001000   // デリバリステータス
  #define ASSERT     0x00004000   // アサート割り込み (vs deassert)
  #define DEASSERT   0x00000000
  #define LEVEL      0x00008000   // レベルトリガ
  #define BCAST      0x00080000   // 自分を含め、すべてのAPICに送信
  #define BUSY       0x00001000
  #define FIXED      0x00000000
#define ICRHI   (0x0310/4)   // 割り込みコマンド [63:32]
#define TIMER   (0x0320/4)   // ローカルベクタテーブル 0 (TIMER)
  #define X1         0x0000000B   // カウントを1で割る
  #define PERIODIC   0x00020000   // 定期的
#define PCINT   (0x0340/4)   // 性能モニタリングカウンタLVT
#define LINT0   (0x0350/4)   // ローカルベクタテーブル 1 (LINT0)
#define LINT1   (0x0360/4)   // ローカルベクタテーブル 2 (LINT1)
#define ERROR   (0x0370/4)   // ローカルベクタテーブル 3 (ERROR)
  #define MASKED     0x00010000   // 割り込みマスク
#define TICR    (0x0380/4)   // タイマー初期カウント
#define TCCR    (0x0390/4)   // タイマーカレントカウント
#define TDCR    (0x03E0/4)   // タイマー除算設定

volatile uint *lapic;  // mp.cで初期化される

//PAGEBREAK!
static void
lapicw(int index, int value)
{
  lapic[index] = value;
  lapic[ID];  // 読み込むことで、書き込みの完了を待つ
}

void
lapicinit(void)
{
  if(!lapic)
    return;

  // ローカルAPICを有効化; スプリアス割り込みベクタをセットする。
  lapicw(SVR, ENABLE | (T_IRQ0 + IRQ_SPURIOUS));

  // タイマーはバス周波数でlapic[TICR]から繰り返しカウントダウンし、
  // 割り込みを発行する。
  // xv6でもっと正確な時間管理をしたいのなら、
  // 外部のタイムソースを使ってTICRを補正するとよいだろう。
  lapicw(TDCR, X1);
  lapicw(TIMER, PERIODIC | (T_IRQ0 + IRQ_TIMER));
  lapicw(TICR, 10000000);

  // 論理割り込みラインを無効化する。
  lapicw(LINT0, MASKED);
  lapicw(LINT1, MASKED);

  // 性能モニタリングカウンタオーバーフロー割り込みエントリが提供
  // されているマシンで、当該割り込みを無効化する。
  if(((lapic[VER]>>16) & 0xFF) >= 4)
    lapicw(PCINT, MASKED);

  // エラー割り込みをIRQ_ERRORにマッピングする。
  lapicw(ERROR, T_IRQ0 + IRQ_ERROR);

  // エラーステータスレジスタをクリアする（連続書き込みが必要）
  lapicw(ESR, 0);
  lapicw(ESR, 0);

  // 未処理のすべての割り込みを確認する。
  lapicw(EOI, 0);

  // Init/レベルトリガ/レベルデアサートを送信して、アービトレーションIDを同期する。
  lapicw(ICRHI, 0);
  lapicw(ICRLO, BCAST | INIT | LEVEL);
  while(lapic[ICRLO] & DELIVS)
    ;

  // APIC上（プロセッサ上ではない）での割り込みを有効化する。
  lapicw(TPR, 0);
}

int
lapicid(void)
{
  if (!lapic)
    return 0;
  return lapic[ID] >> 24;
}

// 割り込みを確認する。
void
lapiceoi(void)
{
  if(lapic)
    lapicw(EOI, 0);
}

// 指定されたマイクロ秒数だけスピンする。
// 実際のハードウェアではこれを動的に調整したいだろう。
void
microdelay(int us)
{
}

#define CMOS_PORT    0x70
#define CMOS_RETURN  0x71

// addrにあるエントリコードを追加プロセッサで実行開始する
// マルチプロセッサ仕様の付録Bを参照
void
lapicstartap(uchar apicid, uint addr)
{
  int i;
  ushort *wrv;

  // "BSPは、[汎用スタートアップアルゴリズム]の前に、CMOSシャットダウン
  // コードを0AHに、warmリセットベクタ（40:67をベースとするDWORD）を
  // APスタートアップコードを指すように初期化しなければならない。
  outb(CMOS_PORT, 0xF);  // オフセット0xFはシャットダウンコード
  outb(CMOS_PORT+1, 0x0A);
  wrv = (ushort*)P2V((0x40<<4 | 0x67));  // Warmリセットベクタ
  wrv[0] = 0;
  wrv[1] = addr >> 4;

  // "汎用スタートアップアルゴリズム"
  // INIT (レベルトリガ)割り込みを送信して他のCPUをリセットする
  lapicw(ICRHI, apicid<<24);
  lapicw(ICRLO, INIT | LEVEL | ASSERT);
  microdelay(200);
  lapicw(ICRLO, INIT | LEVEL);
  microdelay(100);    // 10msでなければならないが、Bochsは遅すぎる!

  // スタートアップIPIを（2回!)送信してコードに入る。
  // 一般的なハードウェアは、INITによる停止中にある場合、
  // STARTUPを1回しか受け付けないと思われる。そのため、2回目は
  // 無視されるはずであるが、これがIntel公式のアルゴリズムである。
  // Bochsは2回めの送信でエラーコードをはく。Bochsにとっては最悪だ。
  for(i = 0; i < 2; i++){
    lapicw(ICRHI, apicid<<24);
    lapicw(ICRLO, STARTUP | (addr>>12));
    microdelay(200);
  }
}

#define CMOS_STATA   0x0a
#define CMOS_STATB   0x0b
#define CMOS_UIP    (1 << 7)        // 進行中にRTCを更新

#define SECS    0x00
#define MINS    0x02
#define HOURS   0x04
#define DAY     0x07
#define MONTH   0x08
#define YEAR    0x09

static uint cmos_read(uint reg)
{
  outb(CMOS_PORT,  reg);
  microdelay(200);

  return inb(CMOS_RETURN);
}

static void fill_rtcdate(struct rtcdate *r)
{
  r->second = cmos_read(SECS);
  r->minute = cmos_read(MINS);
  r->hour   = cmos_read(HOURS);
  r->day    = cmos_read(DAY);
  r->month  = cmos_read(MONTH);
  r->year   = cmos_read(YEAR);
}

// qemuは24時GWTを使用し、その値はBCDエンコードされているようだ。
void cmostime(struct rtcdate *r)
{
  struct rtcdate t1, t2;
  int sb, bcd;

  sb = cmos_read(CMOS_STATB);

  bcd = (sb & (1 << 2)) == 0;

  // 読み込み中にCMOSが時間を変更しないようにする
  for(;;) {
    fill_rtcdate(&t1);
    if(cmos_read(CMOS_STATA) & CMOS_UIP)
        continue;
    fill_rtcdate(&t2);
    if(memcmp(&t1, &t2, sizeof(t1)) == 0)
      break;
  }

  // 変換する
  if(bcd) {
#define    CONV(x)     (t1.x = ((t1.x >> 4) * 10) + (t1.x & 0xf))
    CONV(second);
    CONV(minute);
    CONV(hour  );
    CONV(day   );
    CONV(month );
    CONV(year  );
#undef     CONV
  }

  *r = t1;
  r->year += 2000;
}
