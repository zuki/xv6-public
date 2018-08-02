// x86のトラップと割り込み関係の定数

// プロセッサで定義:
#define T_DIVIDE         0      // 除算エラー
#define T_DEBUG          1      // デバッグ例外
#define T_NMI            2      // NMI割り込み
#define T_BRKPT          3      // ブレークポイント
#define T_OFLOW          4      // オーバーフロー
#define T_BOUND          5      // BOUND範囲超過
#define T_ILLOP          6      // 無効オペコード
#define T_DEVICE         7      // デバイス使用不可
#define T_DBLFLT         8      // ダブルフォルト
// #define T_COPROC      9      // 予約済（486以降未使用）
#define T_TSS           10      // 無効TSS
#define T_SEGNP         11      // セグメント不在
#define T_STACK         12      // スタックフォルト例外
#define T_GPFLT         13      // 一般保護例外
#define T_PGFLT         14      // ページフォルト
// #define T_RES        15      // 予約済
#define T_FPERR         16      // 浮動小数点エラー
#define T_ALIGN         17      // アライメントチェック
#define T_MCHK          18      // マシンチェック
#define T_SIMDERR       19      // SIMD浮動小数点エラー

// 以下の値は任意に選んだものであるが、プロセッサが定義している
// 例外・割り込みベクタと重ならないように注意している。
#define T_SYSCALL       64      // システムコール
#define T_DEFAULT      500      // キャッチオール

#define T_IRQ0          32      // IRQ 0 はint T_IRQに相当する

#define IRQ_TIMER        0
#define IRQ_KBD          1
#define IRQ_COM1         4
#define IRQ_IDE         14
#define IRQ_ERROR       19
#define IRQ_SPURIOUS    31
