// メモリレイアウト

#define EXTMEM  0x100000            // 拡張メモリの開始アドレス
#define PHYSTOP 0xE000000           // 物理メモリの最上位アドレス
#define DEVSPACE 0xFE000000         // その他のデバイスは高位アドレスにある

// アドレス空間レイアウトの主要なアドレス（レイアウトはvm.cのkmapを参照）
#define KERNBASE 0x80000000         // カーネル仮想アドレスの開始アドレス
#define KERNLINK (KERNBASE+EXTMEM)  // カーネルのリンク先アドレス

#define V2P(a) (((uint) (a)) - KERNBASE)
#define P2V(a) (((void *) (a)) + KERNBASE)

#define V2P_WO(x) ((x) - KERNBASE)    // V2Pと同じだが、キャストはしない
#define P2V_WO(x) ((x) + KERNBASE)    // P2Vと同じだが、キャストはしない
