// このファイルはx86メモリ管理ユニット(MMU)
// のための定義を含んでいる。

// Eflagsレジスタ
#define FL_CF           0x00000001      //  1: キャリーフラグ
#define FL_PF           0x00000004      //  2: パリティフラグ
#define FL_AF           0x00000010      //  4: 補助キャリーフラグ
#define FL_ZF           0x00000040      //  6: ゼロフラグ
#define FL_SF           0x00000080      //  7: サインフラグ
#define FL_TF           0x00000100      //  8: トラップフラグ
#define FL_IF           0x00000200      //  9: 割り込みを有効化
#define FL_DF           0x00000400      // 10: ディレクションフラグ
#define FL_OF           0x00000800      // 11: オーバーフローフラグ
#define FL_IOPL_MASK    0x00003000      // 12-13: I/O特権レベルビットマスク
#define FL_IOPL_0       0x00000000      //   IOPL == 0
#define FL_IOPL_1       0x00001000      //   IOPL == 1
#define FL_IOPL_2       0x00002000      //   IOPL == 2
#define FL_IOPL_3       0x00003000      //   IOPL == 3
#define FL_NT           0x00004000      // 14: ネストタスク
#define FL_RF           0x00010000      // 16: レジュームフラグ
#define FL_VM           0x00020000      // 17: 仮想8086モード
#define FL_AC           0x00040000      // 18: アライメントチェック
#define FL_VIF          0x00080000      // 19: 仮想割り込みフラグ
#define FL_VIP          0x00100000      // 20: 仮想割り込みペンディング
#define FL_ID           0x00200000      // 21: IDフラグ

// コントロールレジスタフラグ
#define CR0_PE          0x00000001      //  0: プロテクトモードを有効化
#define CR0_MP          0x00000002      //  1: モニタコプロセッサ
#define CR0_EM          0x00000004      //  2: エミュレーション
#define CR0_TS          0x00000008      //  3: タスクスイッチ
#define CR0_ET          0x00000010      //  4: 拡張タイプ
#define CR0_NE          0x00000020      //  5: 数値演算エラー
#define CR0_WP          0x00010000      // 16: 書き込み保護
#define CR0_AM          0x00040000      // 18: アライメントマスク
#define CR0_NW          0x20000000      // 29: ノットライトスルー
#define CR0_CD          0x40000000      // 30: キャッシュ無効化
#define CR0_PG          0x80000000      // 31: ページング

#define CR4_PSE         0x00000010      // 4: ページサイズ拡張

// セグメントセレクタの定義
#define SEG_KCODE 1  // カーネルコード
#define SEG_KDATA 2  // カーネルデータとスタック
#define SEG_UCODE 3  // ユーザコード
#define SEG_UDATA 4  // ユーザデータとスタック
#define SEG_TSS   5  // このプロセスのタスクステート

// cpu->gdt[NSEGS] は上記のセグメントを保持する
#define NSEGS     6

#ifndef __ASSEMBLER__
// セグメントディスクリプタ
struct segdesc {
  uint lim_15_0 : 16;  // セグメントリミット値の低位ビット
  uint base_15_0 : 16; // セグメントベースアドレスの低位ビット
  uint base_23_16 : 8; // セグメントベースアドレスの中位ビット
  uint type : 4;       // セグメントタイプ（STS_ 定数を参照）
  uint s : 1;          // 0 = システム, 1 = ・アプリケーション
  uint dpl : 2;        // ディスクリプタの特権レベル
  uint p : 1;          // 存在する
  uint lim_19_16 : 4;  // セグメントリミット値の高位ビット
  uint avl : 1;        // 未使用（ソフトウェアで利用可能）
  uint rsv1 : 1;       // 予約済
  uint db : 1;         // 0 = 16 ビットセグメント, 1 = 32 ビットセグメント
  uint g : 1;          // 単位: セットされるとリミット値が4K倍される
  uint base_31_24 : 8; // セグメントベースアドレスの高位ビット
};

// 普通のセグメント
#define SEG(type, base, lim, dpl) (struct segdesc)    \
{ ((lim) >> 12) & 0xffff, (uint)(base) & 0xffff,      \
  ((uint)(base) >> 16) & 0xff, type, 1, dpl, 1,       \
  (uint)(lim) >> 28, 0, 0, 1, 1, (uint)(base) >> 24 }
#define SEG16(type, base, lim, dpl) (struct segdesc)  \
{ (lim) & 0xffff, (uint)(base) & 0xffff,              \
  ((uint)(base) >> 16) & 0xff, type, 1, dpl, 1,       \
  (uint)(lim) >> 16, 0, 0, 1, 0, (uint)(base) >> 24 }
#endif

#define DPL_USER    0x3     // ユーザDPL

// アプリケーションセグメントのタイプビット
#define STA_X       0x8     // 実行可能セグメント
#define STA_E       0x4     // 拡大縮小（非実行可能セグメント）
#define STA_C       0x4     // コンフォーミングコードセグメント（実行可能のみ）
#define STA_W       0x2     // 書き込み可能（非実行可能セグメント）
#define STA_R       0x2     // 読み取り可能（実行可能セグメント）
#define STA_A       0x1     // アクセス

// システムセグメントのタイプビット
#define STS_T16A    0x1     // 16ビットTSSを利用可能
#define STS_LDT     0x2     // ローカルディスクリプタテーブル
#define STS_T16B    0x3     // ビジーな16ビットTSS
#define STS_CG16    0x4     // 16ビットコールゲート
#define STS_TG      0x5     // タスクゲート / Coum Transmitions
#define STS_IG16    0x6     // 16ビット割り込みゲート
#define STS_TG16    0x7     // 16ビットトラップゲート
#define STS_T32A    0x9     // 32ビットTSSを利用可能
#define STS_T32B    0xB     // ビジーな32ビットTSS
#define STS_CG32    0xC     // 32ビットコールゲート
#define STS_IG32    0xE     // 32ビット割り込みゲート
#define STS_TG32    0xF     // 32ビットトラップゲート

// 仮想アドレス 'la' は次の3つの部分からなる構造をもつ:
//
// +--------10---------+-------10---------+---------12----------+
// | ページディレクトリ| ページテーブル   | ページ内オフセット  |
// |   インデックス    | インデックス     |                     |
// +-------------------+------------------+---------------------+
// \---- PDX(va) ------/\--- PTX(va) -----/

// ページディレクトリインデックス
#define PDX(va)         (((uint)(va) >> PDXSHIFT) & 0x3FF)

// ページテーブルインデックス
#define PTX(va)         (((uint)(va) >> PTXSHIFT) & 0x3FF)

// インデックスとオフセットから仮想アドレスを構築
#define PGADDR(d, t, o) ((uint)((d) << PDXSHIFT | (t) << PTXSHIFT | (o)))

// ページディレクトリとページテーブルの定数
#define NPDENTRIES      1024    // PGDIRあたりのディレクトリエントリの数
#define NPTENTRIES      1024    // ページテーブルあたりのPTEの数
#define PGSIZE          4096    // 1ページによりマップされるバイト数

#define PGSHIFT         12      // log2(PGSIZE)
#define PTXSHIFT        12      // 線形アドレスにおけるPTXのオフセット
#define PDXSHIFT        22      // 線形アドレスにおけるPDXのオフセット

#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))

// ページテーブル/ディレクトリエントリのフラグ (p.308)
#define PTE_P           0x001   // 0: メモリ上に存在
#define PTE_W           0x002   // 1: 書き込み可能
#define PTE_U           0x004   // 2: ユーザ
#define PTE_PWT         0x008   // 3: ライトスルー
#define PTE_PCD         0x010   // 4: キャッシュ禁止
#define PTE_A           0x020   // 5: アクセス
#define PTE_D           0x040   // 6: ダーティ
#define PTE_PS          0x080   // 7: ページサイズ
#define PTE_MBZ         0x180   // 7-8: ビットは0固定

// ページテーブル/ディレクトリエントリ内のアドレス
#define PTE_ADDR(pte)   ((uint)(pte) & ~0xFFF)
#define PTE_FLAGS(pte)  ((uint)(pte) &  0xFFF)

#ifndef __ASSEMBLER__
typedef uint pte_t;

// タスクステートセグメント形式
struct taskstate {
  uint link;         // 旧タスクステートセレクタ
  uint esp0;         // 特権レベルが上がった後の
  ushort ss0;        //   スタックポインタとセグメントセレクタ
  ushort padding1;
  uint *esp1;
  ushort ss1;
  ushort padding2;
  uint *esp2;
  ushort ss2;
  ushort padding3;
  void *cr3;         // ページディレクトリのベース
  uint *eip;         // 直前のタスクスイッチで保存されたステート
  uint eflags;
  uint eax;          // さらに保存されたステート（レジスタ）
  uint ecx;
  uint edx;
  uint ebx;
  uint *esp;
  uint *ebp;
  uint esi;
  uint edi;
  ushort es;         // さらにさらに保存されたステート（セグメントセレクタ）
  ushort padding4;
  ushort cs;
  ushort padding5;
  ushort ss;
  ushort padding6;
  ushort ds;
  ushort padding7;
  ushort fs;
  ushort padding8;
  ushort gs;
  ushort padding9;
  ushort ldt;
  ushort padding10;
  ushort t;          // タスクスイッチを起こしたトラップ
  ushort iomb;       // I/Oマップのベースアドレス
};

// PAGEBREAK: 12
// 割り込みとトラップ用のゲートディスクリプタ
struct gatedesc {
  uint off_15_0 : 16;   // セグメントオフセットの低位16ビット
  uint cs : 16;         // コードセグメントセレクタ
  uint args : 5;        // 引数の数、割り込み/トラップゲートでは0
  uint rsv1 : 3;        // 予約済（0にするべきだと思う）
  uint type : 4;        // タイプ(STS_{TG,IG32,TG32})
  uint s : 1;           // 0固定（システム）
  uint dpl : 2;         // ディスクリプタ（の新しい）特権レベル
  uint p : 1;           // 存在する
  uint off_31_16 : 16;  // セグメントオフセットの高位16ビット
};

// 通常の割り込み/ゲートディスクリプタを設定する。
// - istrap: 1 はトラップ（=例外）ゲート、0 は割り込みゲート。
//   割り込みゲートはFL_FLをクリアするが、トラップゲートはFL_IFをいじらない
// - sel: 割り込み/トラップハンドラ用のコードセグメントセレクタ
// - off: 割り込み/トラップハンドラ用のコードセグメント内のオフセット
// - dpl: ディスクリプタ特権レベル -
//        ソフトウェアがint命令を使ってこの割り込み/トラップゲートを
//        明示的に実行するために必要な特権レベル
#define SETGATE(gate, istrap, sel, off, d)                \
{                                                         \
  (gate).off_15_0 = (uint)(off) & 0xffff;                \
  (gate).cs = (sel);                                      \
  (gate).args = 0;                                        \
  (gate).rsv1 = 0;                                        \
  (gate).type = (istrap) ? STS_TG32 : STS_IG32;           \
  (gate).s = 0;                                           \
  (gate).dpl = (d);                                       \
  (gate).p = 1;                                           \
  (gate).off_31_16 = (uint)(off) >> 16;                  \
}

#endif
